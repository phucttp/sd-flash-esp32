"""
firmware_manager.py
===================
Firmware Manager tab — Git-only workflow (no SD card UI).

    [search _____________]              [+ New FW] [⟳ Refresh]
    ┌────────────────────┬──────────────────────────────────────────┐
    │ FW Tree (35%)      │ Notebook (65%):                          │
    │   Device + Version │   • Info & Git: FW details + SYNC TO GIT │
    │   (FW ID hidden;   │   • Edit / Add: form (device_type,       │
    │    iid = internal) │     version, description, file pickers)  │
    │ ┌Delete┐┌Edit Sel┐ │   • OLED Order: drag preview, apply      │
    │                    │     order on next git push               │
    │ Flash History      │                                          │
    │   (recent first)   │                                          │
    └────────────────────┴──────────────────────────────────────────┘

User workflow: pick FW row on left → review on Info, or click [+ New FW]
→ fill form on Edit → Save → list refreshes. Device Type doubles as the
OLED display name; internal FW ID is auto-generated and never shown.
SD-card operations were removed; firmware is distributed via git push.
"""

from __future__ import annotations

import json
import os
import tkinter as tk
from datetime import datetime
from tkinter import filedialog, messagebox, ttk
from typing import Optional

from modules.crypto import FWEncryptor, load_crypto, validate_key_iv
from modules.oled_preview import OLEDPreviewFrame
from modules.theme import Colors, Fonts, StatusBadge
from modules.utils import generate_short_fw_id, safe_name


class FirmwareManagerTab(ttk.Frame):
    """Merged Add Firmware + Library & SD Card tab."""

    HISTORY_MAX = 50

    def __init__(self, parent, app):
        super().__init__(parent, padding=8)
        self.app = app
        self.HISTORY_FILE = os.path.join(
            os.path.dirname(app._config_file), "flash_history.json"
        )

        # Edit form state (FW ID is auto-generated/internal — user manages
        # firmware by device_type + version, which double as the OLED display)
        self.add_device_type_var = tk.StringVar()
        self.add_version_var = tk.StringVar()
        self.add_description_var = tk.StringVar()
        self.add_app_path = tk.StringVar()
        self.add_boot_path = tk.StringVar()
        self.add_part_path = tk.StringVar()

        # None → add new (auto-generate FW ID on save).
        # str  → editing existing FW with this internal id (UPDATE on save).
        self.fm_edit_id: Optional[str] = None

        self.pack(fill=tk.BOTH, expand=True)
        self._build()

        # Load history & initial list
        self._flash_history = self._history_load()
        self._history_refresh_listbox()
        # Initial list refresh deferred to after _on_login_success completes
        self.after(50, self._refresh_firmware_list)

    # ─────────────────────────── Layout ───────────────────────────

    def _build(self):
        # Top toolbar
        top = ttk.Frame(self)
        top.pack(fill=tk.X, pady=(0, 8))
        top.columnconfigure(0, weight=1)

        self._fw_search_var = tk.StringVar()
        self._fw_search_entry = ttk.Entry(top, textvariable=self._fw_search_var,
                                          font=Fonts.mono_small())
        self._fw_search_entry.grid(row=0, column=0, sticky="ew", padx=(0, 8))
        self._fw_search_entry.insert(0, "Search firmware...")
        self._fw_search_entry.configure(foreground="gray")
        self._fw_search_entry.bind("<FocusIn>", self._on_search_focus_in)
        self._fw_search_entry.bind("<FocusOut>", self._on_search_focus_out)
        self._fw_search_var.trace_add(
            "write", lambda *_: self._filter_fw_tree(self._fw_search_var.get())
        )

        ttk.Button(top, text="+ New FW", style="Success.TButton",
                   command=self._new_fw).grid(row=0, column=1, padx=(0, 4))
        ttk.Button(top, text="⟳ Refresh", style="Secondary.TButton",
                   command=self._refresh_firmware_list).grid(row=0, column=2)

        # Horizontal PanedWindow split
        pw = ttk.PanedWindow(self, orient="horizontal")
        pw.pack(fill=tk.BOTH, expand=True)

        # LEFT pane
        left_wrap = ttk.Frame(pw)
        pw.add(left_wrap, weight=35)
        self._build_left_pane(left_wrap)

        # RIGHT pane (notebook with 2 sub-tabs)
        right_wrap = ttk.Frame(pw)
        pw.add(right_wrap, weight=65)

        nb = ttk.Notebook(right_wrap)
        nb.pack(fill=tk.BOTH, expand=True)

        info_sd_frame = ttk.Frame(nb)
        nb.add(info_sd_frame, text="  Info & Git  ")
        self._build_info_sd_pane(info_sd_frame)

        edit_frame = ttk.Frame(nb)
        nb.add(edit_frame, text="  Edit / Add  ")
        self._build_edit_pane(edit_frame)

        oled_frame = ttk.Frame(nb)
        nb.add(oled_frame, text="  OLED Order  ")
        self._build_oled_pane(oled_frame)

        self._notebook_right = nb
        self._edit_frame_idx = 1  # index of edit/add tab

    def _build_left_pane(self, parent):
        parent.rowconfigure(0, weight=3)
        parent.rowconfigure(2, weight=1)
        parent.columnconfigure(0, weight=1)

        # FW Library tree
        lib_grp = ttk.LabelFrame(parent, text="Firmware Library", padding=5)
        lib_grp.grid(row=0, column=0, sticky="nsew", pady=(0, 4))
        lib_grp.rowconfigure(0, weight=1)
        lib_grp.columnconfigure(0, weight=1)

        # FW ID is internal — tree displays Device Type as primary identity
        # (matches OLED display name) and Version as secondary column.
        # `iid` keeps the internal FW ID so selection logic still works.
        columns = ("version",)
        self.fw_tree = ttk.Treeview(lib_grp, columns=columns,
                                     show="tree headings", selectmode="extended")
        self.fw_tree.heading("#0", text="Device")
        self.fw_tree.heading("version", text="Version")
        self.fw_tree.column("#0", width=180)
        self.fw_tree.column("version", width=90)
        self.fw_tree.grid(row=0, column=0, sticky="nsew")
        self.fw_tree.bind("<<TreeviewSelect>>", self._on_fw_select)

        tree_scroll = ttk.Scrollbar(lib_grp, orient="vertical",
                                     command=self.fw_tree.yview)
        tree_scroll.grid(row=0, column=1, sticky="ns")
        self.fw_tree.configure(yscrollcommand=tree_scroll.set)

        # Backwards compatibility alias used elsewhere
        self.fw_listbox = self.fw_tree

        # Buttons row
        btn_row = ttk.Frame(parent)
        btn_row.grid(row=1, column=0, sticky="ew", pady=4)
        btn_row.columnconfigure(0, weight=1)
        btn_row.columnconfigure(1, weight=1)
        ttk.Button(btn_row, text="Delete", style="Danger.TButton",
                   command=self._delete_firmware).grid(row=0, column=0, sticky="ew", padx=(0, 3))
        ttk.Button(btn_row, text="Edit Selected", style="Secondary.TButton",
                   command=self._edit_selected).grid(row=0, column=1, sticky="ew", padx=(3, 0))

        # Flash History (recent first)
        hist_grp = ttk.LabelFrame(parent, text="Flash History", padding=5)
        hist_grp.grid(row=2, column=0, sticky="nsew", pady=(4, 0))
        hist_grp.rowconfigure(0, weight=1)
        hist_grp.columnconfigure(0, weight=1)

        self._hist_listbox = tk.Listbox(
            hist_grp, height=5, font=Fonts.mono_small(),
            bg=Colors.BG_CARD, fg=Colors.TEXT,
            selectbackground=Colors.PRIMARY, selectforeground=Colors.TEXT_DARK,
            highlightthickness=0, borderwidth=0, relief="flat",
        )
        self._hist_listbox.grid(row=0, column=0, sticky="nsew")
        self._hist_listbox.bind("<<ListboxSelect>>", self._on_hist_select)

    def _build_info_sd_pane(self, parent):
        # Vertical split: detail card on top, SD ops on bottom.
        # Detail card weight=1 (compact when empty), SD ops weight=1 (room for listbox).
        # OLED preview moved to its own sub-tab so it doesn't crowd this view.
        parent.rowconfigure(0, weight=1)
        parent.rowconfigure(1, weight=1)
        parent.columnconfigure(0, weight=1)

        # ---- Detail card (scrollable) ----
        det_wrap = ttk.LabelFrame(parent, text="Firmware Details", padding=8)
        det_wrap.grid(row=0, column=0, sticky="nsew", pady=(0, 6))
        det_wrap.rowconfigure(0, weight=1)
        det_wrap.columnconfigure(0, weight=1)

        det_canvas = tk.Canvas(det_wrap, bg=Colors.BG_CARD,
                                highlightthickness=0, borderwidth=0,
                                height=160)
        det_canvas.grid(row=0, column=0, sticky="nsew")
        det_vscroll = ttk.Scrollbar(det_wrap, orient="vertical",
                                     command=det_canvas.yview)
        det_vscroll.grid(row=0, column=1, sticky="ns")
        det_canvas.configure(yscrollcommand=det_vscroll.set)

        self._det_frame = tk.Frame(det_canvas, bg=Colors.BG_CARD)
        self._det_canvas_win = det_canvas.create_window(
            (0, 0), window=self._det_frame, anchor="nw"
        )
        self._det_frame.bind(
            "<Configure>",
            lambda e: det_canvas.configure(scrollregion=det_canvas.bbox("all"))
        )
        det_canvas.bind("<Configure>",
                        lambda e: det_canvas.itemconfig(self._det_canvas_win, width=e.width))
        det_canvas.bind("<Enter>", lambda e: det_canvas.bind_all(
            "<MouseWheel>",
            lambda ev: det_canvas.yview_scroll(int(-1 * (ev.delta / 120)), "units")))
        det_canvas.bind("<Leave>", lambda e: det_canvas.unbind_all("<MouseWheel>"))

        self._build_detail_fields(self._det_frame)
        self._det_clear_with_placeholder()

        # ---- Git Ops ----
        git_grp = ttk.LabelFrame(parent, text="Git Sync", padding=12)
        git_grp.grid(row=1, column=0, sticky="nsew")
        git_grp.columnconfigure(0, weight=1)
        git_grp.columnconfigure(1, weight=1)

        ttk.Label(
            git_grp,
            text="Push the entire firmware library to the configured Git repo.\n"
                 "ESP32 binaries are encrypted with the AES key set in Settings.",
            style="Muted.TLabel", justify="left", wraplength=520,
        ).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 10))

        self.btn_sync_git = tk.Button(
            git_grp, text="⟶ SYNC TO GIT",
            font=(Fonts.FAMILY, 13, "bold"),
            bg=Colors.PRIMARY, fg=Colors.TEXT,
            relief="flat", borderwidth=0,
            pady=14, cursor="hand2",
            command=self._sync_to_git,
        )
        self.btn_sync_git.grid(row=1, column=0, columnspan=2,
                                sticky="ew", pady=(0, 8))

        # Secondary: delete selected entry
        ttk.Button(git_grp, text="🗑 Delete Selected from Library",
                   style="Danger.TButton",
                   command=self._delete_firmware).grid(
            row=2, column=0, columnspan=2, sticky="ew", pady=2)

    def _build_oled_pane(self, parent):
        """OLED Order sub-tab — display order on host device's OLED FW menu."""
        wrap = ttk.Frame(parent, padding=16)
        wrap.pack(fill=tk.BOTH, expand=True)

        ttk.Label(wrap, text="OLED Display Order",
                  font=(Fonts.FAMILY, 12, "bold")).pack(anchor="w")
        ttk.Label(
            wrap,
            text="Reorder how firmware appears on the host's OLED FW menu. "
                 "Apply Order rewrites index.txt on the SD card.",
            style="Muted.TLabel", wraplength=500, justify="left",
        ).pack(anchor="w", pady=(2, 10))

        self.oled_preview = OLEDPreviewFrame(wrap)
        self.oled_preview.pack(fill=tk.BOTH, expand=True)
        self.oled_preview.set_apply_callback(self._on_oled_order_apply)
        self.oled_preview.set_rename_callback(self._on_oled_rename)

    def _det_clear_with_placeholder(self):
        """Show an empty-state hint instead of blank fields."""
        self._det_clear()
        self._det_name_var.set("(no firmware selected)")
        self._det_desc_var.set("Click a firmware in the list on the left "
                               "to see its details here.")

    def _build_detail_fields(self, df):
        df.columnconfigure(0, weight=0)
        df.columnconfigure(1, weight=1)

        # Name heading
        self._det_name_var = tk.StringVar(value="")
        tk.Label(df, textvariable=self._det_name_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=(Fonts.FAMILY, 14, "bold"),
                 anchor="w").grid(row=0, column=0, columnspan=2, sticky="ew",
                                  padx=8, pady=(6, 4))

        # Badges
        badge_row = tk.Frame(df, bg=Colors.BG_CARD)
        badge_row.grid(row=1, column=0, columnspan=2, sticky="w", padx=8, pady=(0, 8))

        self._det_ver_badge = StatusBadge(badge_row, text="", status="info")
        self._det_ver_badge.pack(side=tk.LEFT, padx=(0, 6))

        self._det_type_var = tk.StringVar(value="")
        tk.Label(badge_row, textvariable=self._det_type_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT_MUTED, font=Fonts.normal()).pack(side=tk.LEFT)

        # Description
        tk.Label(df, text="Description", bg=Colors.BG_CARD, fg=Colors.PRIMARY,
                 font=Fonts.bold(), anchor="w").grid(
            row=2, column=0, columnspan=2, sticky="w", padx=8, pady=(4, 2))

        desc_card = tk.Frame(df, bg=Colors.BG_DARK, padx=12, pady=8)
        desc_card.grid(row=3, column=0, columnspan=2, sticky="ew", padx=8, pady=(0, 8))

        self._det_desc_var = tk.StringVar(value="")
        tk.Label(desc_card, textvariable=self._det_desc_var, bg=Colors.BG_DARK,
                 fg=Colors.TEXT, font=Fonts.normal(), anchor="w",
                 wraplength=400, justify="left").pack(fill=tk.X)

        tk.Frame(df, height=1, bg=Colors.BORDER).grid(
            row=4, column=0, columnspan=2, sticky="ew", padx=8, pady=4)

        # _det_id_var retained as no-op StringVar so existing setters
        # (e.g. _on_hist_select) still write to it without raising; it's
        # never displayed.
        self._det_id_var = tk.StringVar(value="")
        self._det_path_var = tk.StringVar(value="")
        self._det_size_var = tk.StringVar(value="")
        self._det_date_var = tk.StringVar(value="")
        self._det_md5_var = tk.StringVar(value="")

        # Path with copy
        r = 5
        tk.Label(df, text="Local File Path", bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
                 font=Fonts.small(), anchor="w").grid(
            row=r, column=0, columnspan=2, sticky="w", padx=8, pady=(8, 2))
        r += 1
        path_row = tk.Frame(df, bg=Colors.BG_CARD)
        path_row.grid(row=r, column=0, columnspan=2, sticky="ew", padx=8)
        path_row.columnconfigure(0, weight=1)
        tk.Label(path_row, textvariable=self._det_path_var, bg=Colors.BG_DARK,
                 fg=Colors.TEXT, font=Fonts.mono_small(), anchor="w",
                 padx=6, pady=4).grid(row=0, column=0, sticky="ew")
        ttk.Button(path_row, text="\U0001f4cb", width=3, style="Icon.TButton",
                   command=self._copy_det_path).grid(row=0, column=1, padx=(4, 0))

        # Size + Date
        r += 1
        size_date_row = tk.Frame(df, bg=Colors.BG_CARD)
        size_date_row.grid(row=r, column=0, columnspan=2, sticky="ew",
                           padx=8, pady=(8, 0))
        size_date_row.columnconfigure(0, weight=1)
        size_date_row.columnconfigure(1, weight=1)
        tk.Label(size_date_row, text="Size", bg=Colors.BG_CARD,
                 fg=Colors.TEXT_MUTED, font=Fonts.small(),
                 anchor="w").grid(row=0, column=0, sticky="w")
        tk.Label(size_date_row, text="Date Added", bg=Colors.BG_CARD,
                 fg=Colors.TEXT_MUTED, font=Fonts.small(),
                 anchor="w").grid(row=0, column=1, sticky="w")
        tk.Label(size_date_row, textvariable=self._det_size_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=Fonts.bold(),
                 anchor="w").grid(row=1, column=0, sticky="w")
        tk.Label(size_date_row, textvariable=self._det_date_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=Fonts.bold(),
                 anchor="w").grid(row=1, column=1, sticky="w")

        # MD5
        r += 1
        tk.Label(df, text="MD5", bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
                 font=Fonts.small(), anchor="w").grid(
            row=r, column=0, columnspan=2, sticky="w", padx=8, pady=(8, 2))
        r += 1
        tk.Label(df, textvariable=self._det_md5_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=Fonts.mono_small(), anchor="w").grid(
            row=r, column=0, columnspan=2, sticky="ew", padx=8, pady=(0, 8))

    def _build_edit_pane(self, parent):
        frm = ttk.Frame(parent, padding=18)
        frm.pack(fill=tk.BOTH, expand=True)

        _, types_hint = self.app.lib.get_suggestions()

        # Info group — Device Type doubles as the OLED display name, so
        # there's no separate FW ID field shown to the user. Internal ID
        # auto-generated on save via generate_short_fw_id.
        grp_info = ttk.LabelFrame(frm, text="1. Firmware Info", padding=12)
        grp_info.pack(fill=tk.X, pady=(0, 12))
        grp_info.columnconfigure(1, weight=1)

        ttk.Label(grp_info, text="Device Type:").grid(row=0, column=0, sticky="w", pady=6)
        self.combo_type = ttk.Combobox(grp_info, textvariable=self.add_device_type_var,
                                        values=types_hint)
        self.combo_type.grid(row=0, column=1, padx=10, sticky="ew")

        ttk.Label(grp_info, text="Version:").grid(row=1, column=0, sticky="w", pady=6)
        ttk.Entry(grp_info, textvariable=self.add_version_var).grid(
            row=1, column=1, padx=10, sticky="ew")

        ttk.Label(grp_info, text="Description:").grid(row=2, column=0, sticky="w", pady=6)
        ttk.Entry(grp_info, textvariable=self.add_description_var).grid(
            row=2, column=1, padx=10, sticky="ew")

        # Edit-mode indicator: shows internal FW ID only when editing existing.
        self._edit_mode_var = tk.StringVar(value="")
        tk.Label(grp_info, textvariable=self._edit_mode_var,
                 fg=Colors.TEXT_MUTED, font=Fonts.small()).grid(
            row=3, column=0, columnspan=2, sticky="w", pady=(4, 0))

        # Files group
        grp_files = ttk.LabelFrame(frm, text="2. Binary Files", padding=12)
        grp_files.pack(fill=tk.X, pady=(0, 12))
        grp_files.columnconfigure(1, weight=1)

        def file_row(row, label, var, cmd):
            lbl = ttk.Label(grp_files, text=label)
            lbl.grid(row=row, column=0, sticky="w", pady=4)
            ent = ttk.Entry(grp_files, textvariable=var, state="readonly")
            ent.grid(row=row, column=1, padx=10, sticky="ew")
            btn = ttk.Button(grp_files, text="Browse...", style="Secondary.TButton",
                              command=cmd)
            btn.grid(row=row, column=2)
            return lbl, ent, btn

        file_row(0, "Application (FW.bin):", self.add_app_path, self._choose_app)
        self._boot_widgets = file_row(1, "Bootloader (BOTL.bin):",
                                       self.add_boot_path, self._choose_boot)
        self._part_widgets = file_row(2, "Partition (PART.bin):",
                                       self.add_part_path, self._choose_part)

        self._stm32_hint = ttk.Label(
            grp_files,
            text="STM32: Only application binary needed",
            foreground=Colors.SUCCESS,
        )
        self._stm32_hint.grid(row=3, column=0, columnspan=3, sticky="w", pady=(5, 0))
        self._stm32_hint.grid_remove()

        self.add_device_type_var.trace_add("write", self._on_device_type_changed)

        # Actions
        grp_act = ttk.Frame(frm)
        grp_act.pack(fill=tk.X, pady=10)

        ttk.Button(grp_act, text="Clear Form", style="Danger.TButton",
                   command=self._clear_add_form).pack(side=tk.LEFT)
        self.btn_add = ttk.Button(grp_act, text="ADD FIRMWARE", style="Primary.TButton",
                                   width=25, command=self._add_firmware)
        self.btn_add.pack(side=tk.RIGHT)

    # ─────────────────────────── Helpers — Add/Edit form ───────────────────────────

    def _on_search_focus_in(self, e):
        if self._fw_search_var.get() == "Search firmware...":
            self._fw_search_entry.delete(0, tk.END)
            self._fw_search_entry.configure(foreground="")

    def _on_search_focus_out(self, e):
        if not self._fw_search_var.get():
            self._fw_search_entry.insert(0, "Search firmware...")
            self._fw_search_entry.configure(foreground="gray")

    def _choose_app(self):
        p = filedialog.askopenfilename(title="Select Application Binary")
        if p:
            self.add_app_path.set(p)

    def _choose_boot(self):
        p = filedialog.askopenfilename(title="Select Bootloader Binary")
        if p:
            self.add_boot_path.set(p)

    def _choose_part(self):
        p = filedialog.askopenfilename(title="Select Partition Binary")
        if p:
            self.add_part_path.set(p)

    def _is_stm32_selected(self) -> bool:
        return "STM32" in self.add_device_type_var.get().upper()

    def _on_device_type_changed(self, *args):
        if not hasattr(self, "_boot_widgets"):
            return
        is_stm32 = self._is_stm32_selected()
        for widget in self._boot_widgets:
            (widget.grid_remove if is_stm32 else widget.grid)()
        for widget in self._part_widgets:
            (widget.grid_remove if is_stm32 else widget.grid)()
        if is_stm32:
            self._stm32_hint.grid()
            self.add_boot_path.set("")
            self.add_part_path.set("")
        else:
            self._stm32_hint.grid_remove()

    def _clear_add_form(self):
        self.add_device_type_var.set("")
        self.add_version_var.set("")
        self.add_description_var.set("")
        self.add_app_path.set("")
        self.add_boot_path.set("")
        self.add_part_path.set("")
        self.fm_edit_id = None
        self._edit_mode_var.set("")
        self.btn_add.config(text="ADD FIRMWARE", style="Primary.TButton")

    def _new_fw(self):
        """Top-toolbar [+ New FW] — switch to Edit tab and clear form."""
        self._clear_add_form()
        self._notebook_right.select(self._edit_frame_idx)

    def _add_firmware(self):
        device_type = self.add_device_type_var.get().strip()
        version = self.add_version_var.get().strip()
        description = self.add_description_var.get()
        app_path = self.add_app_path.get()
        boot_path = self.add_boot_path.get()
        part_path = self.add_part_path.get()

        if not device_type:
            return messagebox.showerror("Error", "Device Type is required")
        if not version:
            return messagebox.showerror("Error", "Version is required")

        if self.fm_edit_id:
            # Updating existing FW — keep internal id
            fw_id = self.fm_edit_id
            overwrite = True
            if not messagebox.askyesno(
                "Confirm", f"Update {device_type} v{version}?"
            ):
                return
        else:
            # New FW — auto-generate internal id (user never sees it)
            fw_id = generate_short_fw_id(self.app.lib.lib_root)
            overwrite = False

        success, msg = self.app.lib.add_firmware(
            fw_id, device_type, version,
            app_path, boot_path, part_path,
            description=description, overwrite=overwrite,
        )
        if success:
            self.app._log(msg)
            self._clear_add_form()
            self._refresh_firmware_list()
            messagebox.showinfo("Success", msg)
            self._notebook_right.select(0)  # back to Info & Git
        else:
            messagebox.showerror("Error", msg)

    def _edit_selected(self):
        """Move the first selected FW into the edit form + switch tab.
        Internal FW ID is captured in self.fm_edit_id but not shown to user."""
        fw_ids = self._get_selected_fw_ids()
        if not fw_ids:
            return messagebox.showinfo("Edit", "Select a firmware from the list first.")
        fw_id = fw_ids[0]
        fw = self.app.lib.get_firmware(fw_id)
        if not fw:
            return
        self.fm_edit_id = fw_id
        self._edit_mode_var.set(f"Editing existing entry (internal id: {fw_id})")
        self.add_device_type_var.set(fw.get("device_type", ""))
        self.add_version_var.set(fw.get("version", ""))
        self.add_description_var.set(fw.get("description", ""))
        self.btn_add.config(text="UPDATE FIRMWARE", style="Accent.TButton")
        self._notebook_right.select(self._edit_frame_idx)

    # ─────────────────────────── Helpers — Library ───────────────────────────

    def _refresh_firmware_list(self):
        """Flat list view: each row = one firmware shown as 'Device' + 'Version'.
        Tree iid stays as internal FW ID so selection / edit / delete still work,
        but the FW ID is never displayed to the user."""
        if hasattr(self, "fw_tree"):
            for item in self.fw_tree.get_children():
                self.fw_tree.delete(item)

        all_fw = {fw.get("fw_id"): fw for fw in self.app.lib.list_firmwares()}
        oled_config = self.app.settings.get("oled_config", {})
        saved_order = oled_config.get("order", [])

        ordered_ids = []
        for fw_id in saved_order:
            if fw_id in all_fw:
                ordered_ids.append(fw_id)
        for fw_id in all_fw:
            if fw_id not in ordered_ids:
                ordered_ids.append(fw_id)

        if hasattr(self, "fw_tree"):
            for fw_id in ordered_ids:
                fw = all_fw.get(fw_id, {})
                device = fw.get("device_type", "Unknown")
                version = fw.get("version", "")
                self.fw_tree.insert(
                    "", "end", iid=fw_id,
                    text=device, values=(version,),
                )

        # OLED preview display
        oled_items = []
        for fw_id in ordered_ids:
            fw = all_fw.get(fw_id, {})
            device = fw.get("device_type", "")
            version = fw.get("version", "")
            oled_name = f"{device} {version}"[:18]
            oled_items.append((fw_id, oled_name))
        if hasattr(self, "oled_preview"):
            self.oled_preview.set_items(oled_items, "FIRMWARE")

        # Refresh Device Type combobox suggestions (FW ID combo removed)
        _, types = self.app.lib.get_suggestions()
        if hasattr(self, "combo_type"):
            self.combo_type["values"] = types

    def _on_oled_order_apply(self, new_order: list):
        """Save OLED display order to settings + git index. SD path was
        dropped — order is now applied to the next git push only."""
        fw_ids = self.oled_preview.get_fw_ids()
        self.app.settings["oled_config"] = {"order": fw_ids}
        try:
            with open(self.app._config_file, "w") as f:
                json.dump(self.app.settings, f, indent=2, ensure_ascii=False)
        except Exception:
            pass
        self._refresh_firmware_list()
        self.app._log(f"OLED order saved: {fw_ids}")
        messagebox.showinfo(
            "Applied",
            "Order saved.\n\nWill be applied on the next 'Sync to Git'."
        )

    def _on_oled_rename(self, fw_id: str, device_type: str, version: str):
        success, msg = self.app.lib.update_metadata(fw_id, {
            "device_type": device_type, "version": version
        })
        if success:
            self.app._log(f"Renamed {fw_id}: {device_type} {version}")
            self._refresh_firmware_list()
        else:
            self.app._log(f"Error renaming {fw_id}: {msg}")
            messagebox.showerror("Error", f"Failed to update metadata:\n{msg}")

    def _det_clear(self):
        self._det_name_var.set("")
        self._det_ver_badge.set_text("")
        self._det_ver_badge.set_status("muted")
        self._det_type_var.set("")
        self._det_desc_var.set("")
        self._det_id_var.set("")
        self._det_path_var.set("")
        self._det_size_var.set("")
        self._det_date_var.set("")
        self._det_md5_var.set("")

    def _copy_det_path(self):
        path = self._det_path_var.get()
        if path:
            self.app.clipboard_clear()
            self.app.clipboard_append(path)

    def _filter_fw_tree(self, query: str):
        """Flat-tree filter: hide rows that don't match Device or Version."""
        if query == "Search firmware..." or not query.strip():
            self._refresh_firmware_list()
            return
        q = query.lower()
        for item in list(self.fw_tree.get_children()):
            vals = self.fw_tree.item(item, "values")
            text = self.fw_tree.item(item, "text")
            match = q in text.lower() or any(q in str(v).lower() for v in vals)
            if not match:
                self.fw_tree.detach(item)

    def _on_fw_select(self, event=None):
        if not hasattr(self, "fw_tree"):
            return
        selection = self.fw_tree.selection()
        if not selection:
            return
        fw_id = selection[0]
        fw = self.app.lib.get_firmware(fw_id)
        if not fw:
            self._det_clear()
            return

        device = fw.get("device_type", "")
        version = fw.get("version", "")
        self._det_name_var.set(f"{device} {version}".strip())
        self._det_ver_badge.set_text(version)
        self._det_ver_badge.set_status("info")
        self._det_type_var.set(f"Device: {device}")
        self._det_desc_var.set(fw.get("description", "") or "No description")
        self._det_path_var.set(fw.get("_path", ""))
        self._det_md5_var.set(fw.get("md5", ""))

        fw_dir = fw.get("_path", "")
        fw_bin = os.path.join(fw_dir, "FW.bin") if fw_dir else ""
        if fw_bin and os.path.exists(fw_bin):
            size_b = os.path.getsize(fw_bin)
            if size_b >= 1024 * 1024:
                self._det_size_var.set(f"{size_b / 1024 / 1024:.1f} MB")
            else:
                self._det_size_var.set(f"{size_b / 1024:.0f} KB")
            mtime = os.path.getmtime(fw_bin)
            self._det_date_var.set(datetime.fromtimestamp(mtime).strftime("%d/%m/%Y"))
        else:
            self._det_size_var.set("—")
            self._det_date_var.set("—")

    def _get_selected_fw_ids(self) -> list:
        """All selections are firmware iids since the tree is now flat
        (no group headers to skip)."""
        if not hasattr(self, "fw_tree"):
            return []
        return list(self.fw_tree.selection())

    def _delete_firmware(self):
        fw_ids = self._get_selected_fw_ids()
        if not fw_ids:
            return messagebox.showwarning("Warning", "Select firmware(s) to delete")
        if not messagebox.askyesno("Confirm", f"Delete {len(fw_ids)} firmware(s)?"):
            return
        for fw_id in fw_ids:
            success, msg = self.app.lib.delete_firmware(fw_id)
            self.app._log(msg)
            self.app.git.remove_from_index([fw_id])
        self._refresh_firmware_list()

    # ─────────────────────────── Helpers — Git/crypto ───────────────────────────

    def _get_encryptor(self) -> Optional[FWEncryptor]:
        key = self.app.enc_key.get()
        iv = self.app.enc_iv.get()
        valid, msg = validate_key_iv(key, iv)
        if not valid:
            messagebox.showerror("Invalid Key/IV", msg)
            return None
        if not load_crypto():
            messagebox.showerror(
                "Error",
                "Crypto library not available.\nInstall: pip install pycryptodome"
            )
            return None
        try:
            return FWEncryptor(key, iv)
        except Exception as e:
            messagebox.showerror("Error", str(e))
            return None

    def _sync_to_git(self):
        if not self.app.git.is_git_installed():
            return self.app._prompt_git_install()
        url = self.app.git_url_var.get().strip()
        if not url:
            return messagebox.showerror(
                "Error", "Git repository URL not set.\nGo to Settings tab."
            )
        all_fw = self.app.lib.list_firmwares()
        if not all_fw:
            return messagebox.showinfo("Info", "No firmware in library")
        if not messagebox.askyesno(
            "Sync to Git",
            f"Export and push {len(all_fw)} firmware(s) to Git?\n\nRepo: {url}"
        ):
            return
        has_esp32 = any(
            "STM32" not in fw.get("device_type", "").upper() for fw in all_fw
        )
        encryptor = None
        if has_esp32:
            encryptor = self._get_encryptor()
            if not encryptor:
                return
        self.app.git.set_repo_url(url)

        exported = []
        for fw in all_fw:
            fw_id = fw.get("fw_id")
            success, result = self.app.git.export_firmware(
                fw_id, fw["_path"], fw, encryptor,
                progress_callback=self.app._log,
            )
            if success:
                exported.append(result)

        if exported:
            oled_order = self.app.settings.get("oled_config", {}).get("order", [])
            if oled_order:
                id_to_meta = {m.get("fw_id"): m for m in exported}
                ordered = []
                seen = set()
                for fid in oled_order:
                    if fid in id_to_meta and fid not in seen:
                        ordered.append(id_to_meta[fid])
                        seen.add(fid)
                for m in exported:
                    fid = m.get("fw_id", "")
                    if fid not in seen:
                        ordered.append(m)
                        seen.add(fid)
                exported = ordered
            self.app.git.update_index(exported)
            self.app._log(f"Exported {len(exported)} firmware(s)")

        try:
            script = self.app.git.create_push_script()
            os.startfile(script)
            self.app._log("Git sync started...")
        except Exception as e:
            messagebox.showerror("Error", str(e))

    # ─────────────────────────── Helpers — History ───────────────────────────

    def _history_load(self) -> list:
        if os.path.exists(self.HISTORY_FILE):
            try:
                with open(self.HISTORY_FILE, "r", encoding="utf-8") as f:
                    data = json.load(f)
                if isinstance(data, list):
                    return data
            except Exception:
                pass
        return []

    def _history_save(self):
        try:
            with open(self.HISTORY_FILE, "w", encoding="utf-8") as f:
                json.dump(self._flash_history[-self.HISTORY_MAX:], f, indent=2)
        except Exception:
            pass

    def _history_add(self, fw_id: str, fw: dict, sd_path: str, mode: str):
        entry = {
            "fw_id": fw_id,
            "device_type": fw.get("device_type", ""),
            "version": fw.get("version", ""),
            "sd_path": sd_path,
            "mode": mode,
            "time": datetime.now().strftime("%Y-%m-%d %H:%M"),
        }
        self._flash_history.append(entry)
        self._history_save()
        self._history_refresh_listbox()

    def _history_refresh_listbox(self):
        self._hist_listbox.delete(0, tk.END)
        for entry in reversed(self._flash_history):
            device = entry.get("device_type", "?")
            version = entry.get("version", "?")
            mode = entry.get("mode", "?")
            fw_id = entry.get("fw_id", "?")
            tag = "P" if mode == "plain" else "E"
            self._hist_listbox.insert(tk.END, f"[{tag}] {fw_id}  {device} v{version}")

    def _on_hist_select(self, event=None):
        sel = self._hist_listbox.curselection()
        if not sel:
            return
        idx = len(self._flash_history) - 1 - sel[0]
        if idx < 0 or idx >= len(self._flash_history):
            return
        entry = self._flash_history[idx]
        device = entry.get("device_type", "")
        version = entry.get("version", "")
        mode = "Plain" if entry.get("mode") == "plain" else "Encrypted"
        self._det_name_var.set(f"{device} {version}".strip())
        self._det_ver_badge.set_text(version)
        self._det_ver_badge.set_status("success" if mode == "Plain" else "warning")
        self._det_type_var.set(f"Device: {device}" if device else "")
        self._det_desc_var.set(f"Copied as {mode}")
        self._det_id_var.set(entry.get("fw_id", ""))
        self._det_path_var.set(entry.get("sd_path", ""))
        self._det_md5_var.set("")
        self._det_size_var.set(mode)
        self._det_date_var.set(entry.get("time", ""))
