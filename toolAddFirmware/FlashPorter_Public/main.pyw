#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FlashPorter Public Edition
==========================
Standalone firmware management tool with local authentication.
- No cloud/server connection
- Local user authentication (username/password)
- Firmware library management
- SD card operations (plain/encrypted)
- Git sync (incremental)

Author: FlashPorter Team
Version: 2.0.0
"""

import os
import sys
import json
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
from typing import Optional

# Add modules to path
if getattr(sys, 'frozen', False):
    APP_DIR = os.path.dirname(sys.executable)
else:
    APP_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, APP_DIR)

# Import modules
from modules.auth import AuthManager
from modules.crypto import FWEncryptor, load_crypto, validate_key_iv
from modules.firmware_lib import FirmwareLibrary
from modules.sd_card import SDCardManager
from modules.git_sync import GitManager
from modules.oled_preview import OLEDPreviewFrame
from modules.utils import safe_name, FileNames, generate_short_fw_id
from modules.theme import Colors, Fonts, apply_theme, GradientFrame, StatusBadge
# net_flash module is now used only by tabs/netflash.py (NetFlashTab)
from modules.master_client import make_master_client

# Tab classes (Phase 1+: split from monolith main.pyw)
from tabs.settings_tab import SettingsTab
from tabs.netflash import NetFlashTab

# Constants
APP_TITLE = "FlashPorter Public Edition v2.0"
CONFIG_FILE = os.path.join(APP_DIR, "settings.json")


class LoginWindow(tk.Toplevel):
    """Login/Registration window with dark theme."""

    def __init__(self, parent, auth_manager: AuthManager, on_success):
        super().__init__(parent)
        self.auth = auth_manager
        self.on_success = on_success
        self.result = False
        self.parent = parent

        self.title("FlashPorter - Login")
        self.geometry("440x480")
        self.resizable(False, False)
        self.configure(bg=Colors.BG_DARK)

        # Center window
        self.update_idletasks()
        x = (self.winfo_screenwidth() - 440) // 2
        y = (self.winfo_screenheight() - 480) // 2
        self.geometry(f"+{x}+{y}")

        # Don't use transient() - parent is hidden
        # Make modal with grab_set only
        self.grab_set()
        self.focus_force()  # Force focus to this window

        # Prevent closing without login
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self._build_ui()

    def _build_ui(self):
        # Gradient accent bar at top
        gradient_bar = GradientFrame(
            self,
            color1=Colors.PRIMARY,
            color2=Colors.SUCCESS,
            height=4
        )
        gradient_bar.pack(fill=tk.X)

        # Main frame with dark background
        frm = ttk.Frame(self, padding=30)
        frm.pack(fill=tk.BOTH, expand=True)

        # Logo/Title
        title_lbl = tk.Label(
            frm,
            text="FlashPorter",
            font=("Segoe UI", 24, "bold"),
            fg=Colors.PRIMARY,
            bg=Colors.BG_DARK
        )
        title_lbl.pack(pady=(0, 3))

        # Version badge
        version_badge = StatusBadge(frm, text="v2.0", status="info")
        version_badge.pack(pady=(0, 3))

        # Subtitle based on first run
        if self.auth.is_first_run():
            subtitle = "Create your account"
            self.is_registration = True
        else:
            subtitle = "Login to continue"
            self.is_registration = False

        tk.Label(
            frm,
            text=subtitle,
            font=Fonts.normal(),
            fg=Colors.TEXT_MUTED,
            bg=Colors.BG_DARK
        ).pack(pady=(0, 20))

        # Username
        ttk.Label(frm, text="Username:").pack(anchor="w")
        self.username_var = tk.StringVar()
        self.username_entry = ttk.Entry(frm, textvariable=self.username_var, font=Fonts.normal())
        self.username_entry.pack(fill=tk.X, pady=(0, 12))

        # Password
        ttk.Label(frm, text="Password:").pack(anchor="w")
        self.password_var = tk.StringVar()
        self.password_entry = ttk.Entry(frm, textvariable=self.password_var, show="*", font=Fonts.normal())
        self.password_entry.pack(fill=tk.X, pady=(0, 12))

        # Confirm password (registration only)
        if self.is_registration:
            ttk.Label(frm, text="Confirm Password:").pack(anchor="w")
            self.confirm_var = tk.StringVar()
            self.confirm_entry = ttk.Entry(frm, textvariable=self.confirm_var, show="*", font=Fonts.normal())
            self.confirm_entry.pack(fill=tk.X, pady=(0, 12))

        # Status label with error color
        self.status_label = ttk.Label(frm, text="", foreground=Colors.ERROR)
        self.status_label.pack(pady=5)

        # Buttons
        btn_frame = ttk.Frame(frm)
        btn_frame.pack(fill=tk.X, pady=15)

        btn_text = "Create Account" if self.is_registration else "Login"
        self.submit_btn = ttk.Button(
            btn_frame,
            text=btn_text,
            style="Primary.TButton",
            command=self._on_submit
        )
        self.submit_btn.pack(fill=tk.X, ipady=8)

        # Reset option (login only)
        if not self.is_registration:
            ttk.Button(
                btn_frame,
                text="Forgot Password? (Reset)",
                style="Secondary.TButton",
                command=self._on_reset
            ).pack(fill=tk.X, pady=(10, 0))

        # Bind Enter key
        self.bind("<Return>", lambda e: self._on_submit())
        self.username_entry.focus()

    def _on_submit(self):
        username = self.username_var.get().strip()
        password = self.password_var.get()

        if not username or not password:
            self.status_label.config(text="Please enter username and password")
            return

        if self.is_registration:
            # Registration
            confirm = self.confirm_var.get()
            if password != confirm:
                self.status_label.config(text="Passwords do not match")
                return

            success, msg = self.auth.create_account(username, password)
        else:
            # Login
            success, msg = self.auth.login(username, password)

        if success:
            self.result = True
            self.on_success()
            self.destroy()
        else:
            self.status_label.config(text=msg)

    def _on_reset(self):
        # Ask for master key
        master_key = tk.simpledialog.askstring(
            "Reset Account",
            "Enter master reset key:",
            show="*",
            parent=self
        )

        if master_key:
            success, msg = self.auth.reset_account(master_key)
            if success:
                messagebox.showinfo("Reset", msg)
                # Restart login window
                self.destroy()
            else:
                self.status_label.config(text=msg)

    def _on_close(self):
        if not self.result:
            if messagebox.askyesno("Exit", "Exit application?"):
                self.master.destroy()


class MainApp(tk.Tk):
    """Main application window with dark theme."""

    def __init__(self):
        super().__init__()
        self.withdraw()  # Hide main window until login success
        self.title(APP_TITLE)

        # Apply dark theme FIRST
        apply_theme(self)

        # Load settings first (need lib_path)
        self.settings = self._load_settings()
        self._config_file = CONFIG_FILE  # exposed for tab classes that persist settings

        # Initialize managers with custom paths
        self.auth = AuthManager()
        lib_path = self.settings.get("lib_path", "")
        self.lib = FirmwareLibrary(lib_path if lib_path else None)
        self.sd = SDCardManager()
        self.git = GitManager()
        # NetFlash backend — mock by default until ESP32-C3 master firmware ready
        self.master_client = make_master_client(
            self.settings.get("netflash_backend", "mock")
        )

        # Variables
        self.enc_key = tk.StringVar(value=self.settings.get("key", ""))
        self.enc_iv = tk.StringVar(value=self.settings.get("iv", ""))
        self.sd_path_var = tk.StringVar(value=self.settings.get("sd_path", ""))
        self.git_url_var = tk.StringVar(value=self.settings.get("git_url", ""))
        self.lib_path_var = tk.StringVar(value=self.settings.get("lib_path", self.lib.lib_root))

        # NetFlash state is now owned by NetFlashTab (Phase 2 redesign).

        # Add firmware variables
        self.add_fw_id_var = tk.StringVar()
        self.add_device_type_var = tk.StringVar()
        self.add_version_var = tk.StringVar()
        self.add_description_var = tk.StringVar()  # Description for Desc tab
        self.add_app_path = tk.StringVar()
        self.add_boot_path = tk.StringVar()
        self.add_part_path = tk.StringVar()

        # Show login
        self._show_login()

    def _show_login(self):
        """Show login window."""
        login = LoginWindow(self, self.auth, self._on_login_success)
        self.wait_window(login)

        if not login.result:
            self.destroy()

    def _on_login_success(self):
        """Called after successful login."""
        self.title(f"{APP_TITLE} - {self.auth.get_username()}")
        self.geometry("1100x750")
        self.deiconify()  # Show main window after login

        # Center window
        self.update_idletasks()
        x = (self.winfo_screenwidth() - 1100) // 2
        y = (self.winfo_screenheight() - 750) // 2
        self.geometry(f"+{x}+{y}")

        # Check Git
        if not self.git.is_git_installed():
            self._prompt_git_install()

        self._build_ui()
        self._refresh_firmware_list()

    def _prompt_git_install(self):
        """Prompt user to install Git if not present."""
        _, msg = self.git.install_git_prompt()

        if messagebox.askyesno("Git Not Found", msg + "\n\nOpen installer?"):
            script = self.git.create_git_install_script()
            os.startfile(script)

    def _build_ui(self):
        """Build main UI with dark theme."""
        # Theme is already applied in __init__

        # Notebook (tabs)
        nb = ttk.Notebook(self)
        nb.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        # Tab 1: Add Firmware
        frm_add = ttk.Frame(nb)
        nb.add(frm_add, text="  + Add Firmware  ")
        self._build_add_tab(frm_add)

        # Tab 2: Library & SD Card
        frm_manage = ttk.Frame(nb)
        nb.add(frm_manage, text="  Library & SD Card  ")
        self._build_manage_tab(frm_manage)

        # Tab 3: Settings (split into tabs/settings_tab.py, Phase 1)
        frm_settings = ttk.Frame(nb)
        nb.add(frm_settings, text="  Settings  ")
        self.settings_tab = SettingsTab(frm_settings, self, CONFIG_FILE)

        # Tab 4: NetFlash (master-slave redesign, Phase 2)
        frm_netflash = ttk.Frame(nb)
        nb.add(frm_netflash, text="  NetFlash  ")
        self.netflash_tab = NetFlashTab(frm_netflash, self, self.master_client)

        # Log area with dark theme
        log_frame = ttk.Frame(self)
        log_frame.pack(fill=tk.BOTH, expand=False, padx=8, pady=(0, 8))

        log_header = ttk.Frame(log_frame)
        log_header.pack(fill=tk.X, pady=(2, 0))
        ttk.Label(log_header, text="Log", style="Title.TLabel").pack(side=tk.LEFT)

        self.log = scrolledtext.ScrolledText(
            log_frame,
            height=5,
            font=Fonts.mono_small(),
            bg=Colors.BG_CARD,
            fg=Colors.TEXT,
            insertbackground=Colors.TEXT,
            selectbackground=Colors.PRIMARY,
            selectforeground=Colors.TEXT_DARK,
            relief="flat",
            borderwidth=0,
            padx=10,
            pady=8
        )
        self.log.pack(fill=tk.BOTH, expand=True, pady=(5, 0))

    def _build_add_tab(self, parent):
        """Build Add Firmware tab."""
        frm = ttk.Frame(parent, padding=20)
        frm.pack(fill=tk.BOTH, expand=True)

        # Get suggestions
        ids_hint, types_hint = self.lib.get_suggestions()

        # Group 1: Info
        grp_info = ttk.LabelFrame(frm, text="1. Firmware Info", padding=15)
        grp_info.pack(fill=tk.X, pady=(0, 15))
        grp_info.columnconfigure(1, weight=1)

        # FW ID with "New" button
        ttk.Label(grp_info, text="Firmware ID:").grid(row=0, column=0, sticky="w", pady=8)
        self.combo_id = ttk.Combobox(grp_info, textvariable=self.add_fw_id_var, values=ids_hint)
        self.combo_id.grid(row=0, column=1, padx=10, sticky="ew")
        self.combo_id.bind("<<ComboboxSelected>>", self._on_id_selected)
        ttk.Button(grp_info, text="New", style="Success.TButton",
                   command=self._new_fw_id, width=6).grid(row=0, column=2, pady=8)

        # Realtime duplicate detection
        self.add_fw_id_var.trace_add("write", self._on_fw_id_changed)

        # Device Type
        ttk.Label(grp_info, text="Device Type:").grid(row=1, column=0, sticky="w", pady=8)
        self.combo_type = ttk.Combobox(grp_info, textvariable=self.add_device_type_var, values=types_hint)
        self.combo_type.grid(row=1, column=1, padx=10, sticky="ew")

        # Version
        ttk.Label(grp_info, text="Version:").grid(row=2, column=0, sticky="w", pady=8)
        ttk.Entry(grp_info, textvariable=self.add_version_var).grid(row=2, column=1, padx=10, sticky="ew")

        # Description (shown on OLED Status tab)
        ttk.Label(grp_info, text="Description:").grid(row=3, column=0, sticky="w", pady=8)
        ttk.Entry(grp_info, textvariable=self.add_description_var).grid(row=3, column=1, padx=10, sticky="ew")

        # Group 2: Files
        grp_files = ttk.LabelFrame(frm, text="2. Binary Files", padding=15)
        grp_files.pack(fill=tk.X, pady=(0, 20))
        grp_files.columnconfigure(1, weight=1)

        def file_row(row, label, var, cmd):
            lbl = ttk.Label(grp_files, text=label)
            lbl.grid(row=row, column=0, sticky="w", pady=5)
            ent = ttk.Entry(grp_files, textvariable=var, state="readonly")
            ent.grid(row=row, column=1, padx=10, sticky="ew")
            btn = ttk.Button(grp_files, text="Browse...", style="Secondary.TButton", command=cmd)
            btn.grid(row=row, column=2)
            return lbl, ent, btn

        file_row(0, "Application (FW.bin):", self.add_app_path, self._choose_app)
        self._boot_widgets = file_row(1, "Bootloader (BOTL.bin):", self.add_boot_path, self._choose_boot)
        self._part_widgets = file_row(2, "Partition (PART.bin):", self.add_part_path, self._choose_part)

        # STM32 hint label (hidden by default)
        self._stm32_hint = ttk.Label(grp_files, text="STM32: Only application binary needed",
                                      foreground=Colors.SUCCESS)
        self._stm32_hint.grid(row=3, column=0, columnspan=3, sticky="w", pady=(5, 0))
        self._stm32_hint.grid_remove()

        # Auto show/hide boot/part fields based on device_type
        self.add_device_type_var.trace_add("write", self._on_device_type_changed)

        # Actions
        grp_act = ttk.Frame(frm)
        grp_act.pack(fill=tk.X, pady=10)

        ttk.Button(grp_act, text="Clear Form", style="Danger.TButton", command=self._clear_add_form).pack(side=tk.LEFT)
        self.btn_add = ttk.Button(grp_act, text="ADD FIRMWARE", style="Primary.TButton", width=25, command=self._add_firmware)
        self.btn_add.pack(side=tk.RIGHT)

    def _build_manage_tab(self, parent):
        """Build Library & SD Card management tab with grid layout."""
        frm = ttk.Frame(parent, padding=10)
        frm.pack(fill=tk.BOTH, expand=True)

        # Configure grid weights
        frm.columnconfigure(0, weight=1)  # Firmware tree
        frm.columnconfigure(1, weight=0)  # OLED + Actions
        frm.columnconfigure(2, weight=1)  # Details
        frm.rowconfigure(0, weight=1)

        # ========== LEFT: Firmware Treeview ==========
        left = ttk.LabelFrame(frm, text="Firmware Library", padding=5)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 5))
        left.rowconfigure(1, weight=1)
        left.columnconfigure(0, weight=1)

        # Search bar
        self._fw_search_var = tk.StringVar()
        self._fw_search_entry = ttk.Entry(left, textvariable=self._fw_search_var,
                                           font=Fonts.mono_small())
        self._fw_search_entry.grid(row=0, column=0, columnspan=2, sticky="ew",
                                    pady=(0, 5))
        self._fw_search_entry.insert(0, "Search firmware...")
        self._fw_search_entry.configure(foreground="gray")

        def _search_focus_in(e):
            if self._fw_search_var.get() == "Search firmware...":
                self._fw_search_entry.delete(0, tk.END)
                self._fw_search_entry.configure(foreground="")
        def _search_focus_out(e):
            if not self._fw_search_var.get():
                self._fw_search_entry.insert(0, "Search firmware...")
                self._fw_search_entry.configure(foreground="gray")

        self._fw_search_entry.bind("<FocusIn>", _search_focus_in)
        self._fw_search_entry.bind("<FocusOut>", _search_focus_out)
        self._fw_search_var.trace_add("write",
            lambda *_: self._filter_fw_tree(self._fw_search_var.get()))

        # Treeview with columns
        columns = ("device", "version")
        self.fw_tree = ttk.Treeview(left, columns=columns, show="tree headings",
                                     selectmode="extended")
        self.fw_tree.heading("#0", text="Firmware ID")
        self.fw_tree.heading("device", text="Device")
        self.fw_tree.heading("version", text="Version")
        self.fw_tree.column("#0", width=100)
        self.fw_tree.column("device", width=80)
        self.fw_tree.column("version", width=60)
        self.fw_tree.grid(row=1, column=0, sticky="nsew")
        self.fw_tree.bind("<<TreeviewSelect>>", self._on_fw_select)

        # Scrollbar for treeview
        tree_scroll = ttk.Scrollbar(left, orient="vertical", command=self.fw_tree.yview)
        tree_scroll.grid(row=1, column=1, sticky="ns")
        self.fw_tree.configure(yscrollcommand=tree_scroll.set)

        # Buttons under tree
        btn_tree = ttk.Frame(left)
        btn_tree.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(5, 0))
        btn_tree.columnconfigure(0, weight=1)
        btn_tree.columnconfigure(1, weight=1)

        ttk.Button(btn_tree, text="Delete", style="Danger.TButton",
                   command=self._delete_firmware).grid(row=0, column=0, sticky="ew", padx=(0, 3))
        ttk.Button(btn_tree, text="Refresh", style="Secondary.TButton",
                   command=self._refresh_firmware_list).grid(row=0, column=1, sticky="ew", padx=(3, 0))

        # ========== MIDDLE: OLED Preview + Actions ==========
        mid = ttk.Frame(frm)
        mid.grid(row=0, column=1, sticky="ns", padx=5)

        # OLED Preview
        self.oled_preview = OLEDPreviewFrame(mid)
        self.oled_preview.grid(row=0, column=0, sticky="ew", pady=(0, 5))
        self.oled_preview.set_apply_callback(self._on_oled_order_apply)
        self.oled_preview.set_rename_callback(self._on_oled_rename)

        # Actions frame with grid
        grp_act = ttk.LabelFrame(mid, text="Actions", padding=8)
        grp_act.grid(row=1, column=0, sticky="ew", pady=(0, 5))
        grp_act.columnconfigure(0, weight=1)
        grp_act.columnconfigure(1, weight=1)

        BTN_W = 14
        # Row 0: SD Card path
        ttk.Label(grp_act, text="SD Card:").grid(row=0, column=0, sticky="w")
        sd_frame = ttk.Frame(grp_act)
        sd_frame.grid(row=0, column=1, sticky="ew", pady=2)
        sd_frame.columnconfigure(0, weight=1)
        ttk.Entry(sd_frame, textvariable=self.sd_path_var, width=15).grid(row=0, column=0, sticky="ew")
        ttk.Button(sd_frame, text="...", width=3, style="Icon.TButton",
                   command=self._choose_sd_path).grid(row=0, column=1, padx=(2, 0))

        # Row 1-2: Copy buttons
        ttk.Button(grp_act, text="Copy Plain", width=BTN_W, style="Success.TButton",
                   command=self._copy_to_sd_plain).grid(row=1, column=0, sticky="ew", padx=2, pady=3)
        ttk.Button(grp_act, text="Copy Encrypted", width=BTN_W, style="Accent.TButton",
                   command=self._copy_to_sd_enc).grid(row=1, column=1, sticky="ew", padx=2, pady=3)

        # Row 3: Git sync (single button)
        ttk.Button(grp_act, text="SYNC TO GIT", width=BTN_W*2, style="Primary.TButton",
                   command=self._sync_to_git).grid(row=2, column=0, columnspan=2, sticky="ew", padx=2, pady=3)

        # Row 4: Remove
        ttk.Button(grp_act, text="Remove from SD", style="Danger.TButton",
                   command=self._remove_from_sd).grid(row=3, column=0, columnspan=2, sticky="ew", padx=2, pady=3)

        # SD Card Contents (compact)
        grp_sd = ttk.LabelFrame(mid, text="SD Card", padding=5)
        grp_sd.grid(row=2, column=0, sticky="nsew", pady=(0, 5))
        grp_sd.rowconfigure(1, weight=1)
        grp_sd.columnconfigure(0, weight=1)

        ttk.Button(grp_sd, text="Load SD", style="Secondary.TButton",
                   command=self._load_sd_card).grid(row=0, column=0, sticky="ew")
        self.sd_listbox = tk.Listbox(
            grp_sd,
            height=5,
            font=Fonts.mono_small(),
            bg=Colors.BG_CARD,
            fg=Colors.TEXT,
            selectbackground=Colors.PRIMARY,
            selectforeground=Colors.TEXT_DARK,
            highlightthickness=0,
            borderwidth=0,
            relief="flat"
        )
        self.sd_listbox.grid(row=1, column=0, sticky="nsew", pady=(5, 0))
        self.sd_listbox.bind("<<ListboxSelect>>", self._on_sd_select)

        # Flash History (compact)
        grp_hist = ttk.LabelFrame(mid, text="Flash History", padding=5)
        grp_hist.grid(row=3, column=0, sticky="nsew", pady=(0, 5))
        grp_hist.rowconfigure(0, weight=1)
        grp_hist.columnconfigure(0, weight=1)

        self._hist_listbox = tk.Listbox(
            grp_hist,
            height=4,
            font=Fonts.mono_small(),
            bg=Colors.BG_CARD,
            fg=Colors.TEXT,
            selectbackground=Colors.PRIMARY,
            selectforeground=Colors.TEXT_DARK,
            highlightthickness=0,
            borderwidth=0,
            relief="flat"
        )
        self._hist_listbox.grid(row=0, column=0, sticky="nsew")
        self._hist_listbox.bind("<<ListboxSelect>>", self._on_hist_select)

        # Load history on startup
        self._flash_history = self._history_load()
        self._history_refresh_listbox()

        # ========== RIGHT: Firmware Details ==========
        right = ttk.LabelFrame(frm, text="Firmware Details", padding=10)
        right.grid(row=0, column=2, sticky="nsew", padx=(5, 0))
        right.rowconfigure(0, weight=1)
        right.columnconfigure(0, weight=1)

        # Scrollable detail panel
        det_canvas = tk.Canvas(right, bg=Colors.BG_CARD, highlightthickness=0,
                               borderwidth=0)
        det_canvas.grid(row=0, column=0, sticky="nsew")
        det_vscroll = ttk.Scrollbar(right, orient="vertical", command=det_canvas.yview)
        det_vscroll.grid(row=0, column=1, sticky="ns")
        det_canvas.configure(yscrollcommand=det_vscroll.set)

        self._det_frame = tk.Frame(det_canvas, bg=Colors.BG_CARD)
        self._det_canvas_win = det_canvas.create_window((0, 0), window=self._det_frame,
                                                         anchor="nw")
        self._det_canvas = det_canvas

        def _det_configure(e):
            det_canvas.configure(scrollregion=det_canvas.bbox("all"))
        self._det_frame.bind("<Configure>", _det_configure)
        det_canvas.bind("<Configure>",
                        lambda e: det_canvas.itemconfig(self._det_canvas_win, width=e.width))
        # Mousewheel
        det_canvas.bind("<Enter>",
                        lambda e: det_canvas.bind_all("<MouseWheel>",
                                                       lambda ev: det_canvas.yview_scroll(
                                                           int(-1*(ev.delta/120)), "units")))
        det_canvas.bind("<Leave>", lambda e: det_canvas.unbind_all("<MouseWheel>"))

        df = self._det_frame
        df.columnconfigure(0, weight=0)
        df.columnconfigure(1, weight=1)

        # Row 0: Device name (large bold heading)
        self._det_name_var = tk.StringVar(value="")
        tk.Label(df, textvariable=self._det_name_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=(Fonts.FAMILY, 16, "bold"),
                 anchor="w").grid(row=0, column=0, columnspan=2, sticky="ew",
                                  padx=8, pady=(8, 4))

        # Row 1: Badges — [version] [device type]
        badge_row = tk.Frame(df, bg=Colors.BG_CARD)
        badge_row.grid(row=1, column=0, columnspan=2, sticky="w", padx=8, pady=(0, 8))

        self._det_ver_badge = StatusBadge(badge_row, text="", status="info")
        self._det_ver_badge.pack(side=tk.LEFT, padx=(0, 6))

        self._det_type_var = tk.StringVar(value="")
        tk.Label(badge_row, textvariable=self._det_type_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT_MUTED, font=Fonts.normal()).pack(side=tk.LEFT)

        # Row 2: Description label
        tk.Label(df, text="Description", bg=Colors.BG_CARD, fg=Colors.PRIMARY,
                 font=Fonts.bold(), anchor="w").grid(
            row=2, column=0, columnspan=2, sticky="w", padx=8, pady=(4, 2))

        # Row 3: Description card
        desc_card = tk.Frame(df, bg=Colors.BG_DARK, padx=12, pady=8)
        desc_card.grid(row=3, column=0, columnspan=2, sticky="ew", padx=8, pady=(0, 8))

        self._det_desc_var = tk.StringVar(value="")
        tk.Label(desc_card, textvariable=self._det_desc_var, bg=Colors.BG_DARK,
                 fg=Colors.TEXT, font=Fonts.normal(), anchor="w",
                 wraplength=220, justify="left").pack(fill=tk.X)

        # Row 4: Separator
        tk.Frame(df, height=1, bg=Colors.BORDER).grid(
            row=4, column=0, columnspan=2, sticky="ew", padx=8, pady=4)

        # Row 5-10: Info grid (label: value)
        info_fields = [
            ("ID:", "_det_id_var"),
            ("Local File Path", "_det_path_label"),
            ("Size", "_det_size_var"),
            ("Date Added", "_det_date_var"),
            ("MD5", "_det_md5_var"),
        ]

        self._det_id_var = tk.StringVar(value="")
        self._det_path_var = tk.StringVar(value="")
        self._det_size_var = tk.StringVar(value="")
        self._det_date_var = tk.StringVar(value="")
        self._det_md5_var = tk.StringVar(value="")

        r = 5
        # ID
        tk.Label(df, text="ID:", bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
                 font=Fonts.small(), anchor="w").grid(row=r, column=0, sticky="w", padx=(8, 4))
        tk.Label(df, textvariable=self._det_id_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=Fonts.mono_small(), anchor="w").grid(
            row=r, column=1, sticky="ew", padx=(0, 8))

        # Local File Path (with copy button)
        r += 1
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

        # Size + Date on same row
        r += 1
        size_date_row = tk.Frame(df, bg=Colors.BG_CARD)
        size_date_row.grid(row=r, column=0, columnspan=2, sticky="ew",
                           padx=8, pady=(8, 0))
        size_date_row.columnconfigure(0, weight=1)
        size_date_row.columnconfigure(1, weight=1)

        tk.Label(size_date_row, text="Size", bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
                 font=Fonts.small(), anchor="w").grid(row=0, column=0, sticky="w")
        tk.Label(size_date_row, text="Date Added", bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
                 font=Fonts.small(), anchor="w").grid(row=0, column=1, sticky="w")
        tk.Label(size_date_row, textvariable=self._det_size_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=Fonts.bold(), anchor="w").grid(row=1, column=0, sticky="w")
        tk.Label(size_date_row, textvariable=self._det_date_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=Fonts.bold(), anchor="w").grid(row=1, column=1, sticky="w")

        # MD5
        r += 1
        tk.Label(df, text="MD5", bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
                 font=Fonts.small(), anchor="w").grid(
            row=r, column=0, columnspan=2, sticky="w", padx=8, pady=(8, 2))
        r += 1
        tk.Label(df, textvariable=self._det_md5_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=Fonts.mono_small(), anchor="w").grid(
            row=r, column=0, columnspan=2, sticky="ew", padx=8, pady=(0, 8))

        # Keep compatibility with old listbox-based code
        self.fw_listbox = self.fw_tree  # Alias for compatibility

    # ==================== Helper Methods ====================

    def _log(self, msg: str):
        """Log message to log area."""
        self.log.insert(tk.END, msg + "\n")
        self.log.see(tk.END)
        self.update()

    def _load_settings(self) -> dict:
        """Load settings from file."""
        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, "r") as f:
                    return json.load(f)
            except:
                pass
        return {}

    # ==================== Add Tab Methods ====================

    def _choose_app(self):
        p = filedialog.askopenfilename(title="Select Application Binary")
        if p: self.add_app_path.set(p)

    def _choose_boot(self):
        p = filedialog.askopenfilename(title="Select Bootloader Binary")
        if p: self.add_boot_path.set(p)

    def _choose_part(self):
        p = filedialog.askopenfilename(title="Select Partition Binary")
        if p: self.add_part_path.set(p)

    def _is_stm32_selected(self) -> bool:
        """Check if current device_type is STM32."""
        return "STM32" in self.add_device_type_var.get().upper()

    def _on_device_type_changed(self, *args):
        """Show/hide bootloader & partition fields based on device_type."""
        if not hasattr(self, '_boot_widgets'):
            return
        is_stm32 = self._is_stm32_selected()
        for widget in self._boot_widgets:
            if is_stm32:
                widget.grid_remove()
            else:
                widget.grid()
        for widget in self._part_widgets:
            if is_stm32:
                widget.grid_remove()
            else:
                widget.grid()
        if is_stm32:
            self._stm32_hint.grid()
            self.add_boot_path.set("")
            self.add_part_path.set("")
        else:
            self._stm32_hint.grid_remove()

    def _clear_add_form(self):
        self.add_fw_id_var.set("")
        self.add_device_type_var.set("")
        self.add_version_var.set("")
        self.add_description_var.set("")
        self.add_app_path.set("")
        self.add_boot_path.set("")
        self.add_part_path.set("")
        self.btn_add.config(text="ADD FIRMWARE", style="Primary.TButton")

    def _on_id_selected(self, event=None):
        """When existing ID is selected, fill in details."""
        fw = self.lib.get_firmware(self.add_fw_id_var.get())
        if fw:
            self.add_device_type_var.set(fw.get("device_type", ""))
            self.add_version_var.set(fw.get("version", ""))
            self.add_description_var.set(fw.get("description", ""))
            self.btn_add.config(text="UPDATE FIRMWARE", style="Accent.TButton")
        else:
            self.btn_add.config(text="ADD FIRMWARE", style="Primary.TButton")

    def _new_fw_id(self):
        """Generate next available FW ID and clear form."""
        new_id = generate_short_fw_id(self.lib.lib_root)
        self._clear_add_form()
        self.add_fw_id_var.set(new_id)

    def _on_fw_id_changed(self, *args):
        """Realtime duplicate detection — switch button text ADD/UPDATE."""
        fw_id = safe_name(self.add_fw_id_var.get())
        if not fw_id:
            self.btn_add.config(text="ADD FIRMWARE", style="Primary.TButton")
            return
        existing = self.lib.get_firmware(fw_id)
        if existing:
            self.btn_add.config(text="UPDATE FIRMWARE", style="Accent.TButton")
        else:
            self.btn_add.config(text="ADD FIRMWARE", style="Primary.TButton")

    def _add_firmware(self):
        """Add firmware to library."""
        fw_id = safe_name(self.add_fw_id_var.get())
        device_type = self.add_device_type_var.get()
        version = self.add_version_var.get()
        description = self.add_description_var.get()
        app_path = self.add_app_path.get()
        boot_path = self.add_boot_path.get()
        part_path = self.add_part_path.get()

        if not fw_id:
            return messagebox.showerror("Error", "Firmware ID is required")

        # Check if updating
        existing = self.lib.get_firmware(fw_id)
        overwrite = existing is not None

        if overwrite and not messagebox.askyesno("Confirm", f"Update existing firmware '{fw_id}'?"):
            return

        success, msg = self.lib.add_firmware(
            fw_id, device_type, version,
            app_path, boot_path, part_path,
            description=description,
            overwrite=overwrite
        )

        if success:
            self._log(msg)
            self._clear_add_form()
            self._refresh_firmware_list()
            messagebox.showinfo("Success", msg)
        else:
            messagebox.showerror("Error", msg)

    # ==================== Manage Tab Methods ====================

    def _refresh_firmware_list(self):
        """Refresh firmware treeview and OLED preview."""
        # Clear treeview
        if hasattr(self, 'fw_tree'):
            for item in self.fw_tree.get_children():
                self.fw_tree.delete(item)

        # Get all firmwares
        all_fw = {fw.get("fw_id"): fw for fw in self.lib.list_firmwares()}

        # Load saved OLED config (order only - display names come from metadata)
        oled_config = self.settings.get("oled_config", {})
        saved_order = oled_config.get("order", [])

        # Build ordered list (saved order first, then new items)
        ordered_ids = []
        for fw_id in saved_order:
            if fw_id in all_fw:
                ordered_ids.append(fw_id)

        # Add any new firmware not in saved order
        for fw_id in all_fw:
            if fw_id not in ordered_ids:
                ordered_ids.append(fw_id)

        # Group by device_type for treeview
        device_groups = {}
        for fw_id in ordered_ids:
            fw = all_fw.get(fw_id, {})
            device = fw.get("device_type", "Unknown")
            if device not in device_groups:
                device_groups[device] = []
            device_groups[device].append((fw_id, fw))

        # Add to treeview grouped by device_type
        if hasattr(self, 'fw_tree'):
            for device, items in device_groups.items():
                # Add device group
                group_id = self.fw_tree.insert("", "end", text=f"[{device}]", open=True,
                                                values=("", f"{len(items)} items"))
                # Add firmware items under group
                for fw_id, fw in items:
                    self.fw_tree.insert(group_id, "end", text=fw_id, iid=fw_id,
                                        values=(fw.get("device_type", ""),
                                               fw.get("version", "")))

        # Build display names for OLED (from metadata)
        oled_items = []
        for fw_id in ordered_ids:
            fw = all_fw.get(fw_id, {})
            device = fw.get("device_type", "")
            version = fw.get("version", "")
            oled_name = f"{device} {version}"[:18]  # Leave room for index
            oled_items.append((fw_id, oled_name))

        # Update OLED preview
        if hasattr(self, 'oled_preview'):
            self.oled_preview.set_items(oled_items, "FIRMWARE")

        # Update suggestions
        ids, types = self.lib.get_suggestions()
        if hasattr(self, 'combo_id'):
            self.combo_id['values'] = ids
            self.combo_type['values'] = types

    def _on_oled_order_apply(self, new_order: list):
        """Apply new firmware order — rewrite index.txt on SD card immediately."""
        fw_ids = self.oled_preview.get_fw_ids()

        # 1. Save order to settings (for future copies)
        self.settings["oled_config"] = {"order": fw_ids}
        try:
            with open(CONFIG_FILE, "w") as f:
                json.dump(self.settings, f, indent=2, ensure_ascii=False)
        except Exception:
            pass

        # 2. Rewrite index.txt on SD card NOW
        sd_path = self.sd_path_var.get()
        sd_updated = False
        if sd_path and self.sd.set_path(sd_path):
            success, _ = self.sd.load_index()
            if success and self.sd.index_data:
                self.sd.reorder_index(fw_ids)
                self.sd.save_index()
                sd_updated = True
                self._log(f"SD index.txt rewritten: {[i.get('fw_id','?') for i in self.sd.index_data]}")
                self._load_sd_card()

        # 3. Refresh UI
        self._refresh_firmware_list()

        if sd_updated:
            messagebox.showinfo("Applied",
                f"Order updated!\n\n"
                f"index.txt on SD card rewritten.\n"
                f"OLED will show new order after reboot.")
        else:
            messagebox.showinfo("Applied",
                f"Order saved to settings.\n\n"
                f"Set SD card path and Load SD first\n"
                f"to update index.txt on the card.")

    def _on_oled_rename(self, fw_id: str, device_type: str, version: str):
        """Update firmware metadata when renamed from OLED preview."""
        success, msg = self.lib.update_metadata(fw_id, {
            "device_type": device_type,
            "version": version
        })

        if success:
            self._log(f"Renamed {fw_id}: {device_type} {version}")
            # Refresh to show updated name
            self._refresh_firmware_list()
        else:
            self._log(f"Error renaming {fw_id}: {msg}")
            messagebox.showerror("Error", f"Failed to update metadata:\n{msg}")

    def _det_clear(self):
        """Reset all detail panel fields."""
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
        """Copy firmware path to clipboard."""
        path = self._det_path_var.get()
        if path:
            self.clipboard_clear()
            self.clipboard_append(path)

    def _filter_fw_tree(self, query: str):
        """Filter treeview items by search query."""
        if query == "Search firmware..." or not query.strip():
            self._refresh_firmware_list()
            return
        q = query.lower()
        for group in list(self.fw_tree.get_children()):
            children = list(self.fw_tree.get_children(group))
            visible = 0
            for child in children:
                vals = self.fw_tree.item(child, "values")
                text = self.fw_tree.item(child, "text")
                match = q in text.lower() or any(q in str(v).lower() for v in vals)
                if match:
                    visible += 1
                else:
                    self.fw_tree.detach(child)
            if visible == 0:
                self.fw_tree.detach(group)

    def _on_fw_select(self, event=None):
        """Show firmware details in structured panel."""
        if not hasattr(self, 'fw_tree'):
            return

        selection = self.fw_tree.selection()
        if not selection:
            return

        fw_id = selection[0]
        if fw_id.startswith("I"):
            return

        fw = self.lib.get_firmware(fw_id)
        if not fw:
            self._det_clear()
            return

        device = fw.get('device_type', '')
        version = fw.get('version', '')
        self._det_name_var.set(f"{device} {version}".strip())
        self._det_ver_badge.set_text(version)
        self._det_ver_badge.set_status("info")
        self._det_type_var.set(f"Device: {device}")
        self._det_desc_var.set(fw.get('description', '') or 'No description')
        self._det_id_var.set(fw.get('fw_id', fw_id))
        self._det_path_var.set(fw.get('_path', ''))
        self._det_md5_var.set(fw.get('md5', ''))

        fw_dir = fw.get('_path', '')
        fw_bin = os.path.join(fw_dir, 'FW.bin') if fw_dir else ''
        if fw_bin and os.path.exists(fw_bin):
            size_b = os.path.getsize(fw_bin)
            if size_b >= 1024 * 1024:
                self._det_size_var.set(f"{size_b / 1024 / 1024:.1f} MB")
            else:
                self._det_size_var.set(f"{size_b / 1024:.0f} KB")
            from datetime import datetime
            mtime = os.path.getmtime(fw_bin)
            self._det_date_var.set(datetime.fromtimestamp(mtime).strftime("%d/%m/%Y"))
        else:
            self._det_size_var.set("\u2014")
            self._det_date_var.set("\u2014")

    def _get_selected_fw_ids(self) -> list:
        """Get selected firmware IDs from treeview (excluding group headers)."""
        if not hasattr(self, 'fw_tree'):
            return []

        fw_ids = []
        for item_id in self.fw_tree.selection():
            # Skip group headers (they don't have fw_id in library)
            if not item_id.startswith("I"):
                fw_ids.append(item_id)
        return fw_ids

    def _delete_firmware(self):
        """Delete selected firmware(s)."""
        fw_ids = self._get_selected_fw_ids()
        if not fw_ids:
            return messagebox.showwarning("Warning", "Select firmware(s) to delete")

        if not messagebox.askyesno("Confirm", f"Delete {len(fw_ids)} firmware(s)?"):
            return

        for fw_id in fw_ids:
            success, msg = self.lib.delete_firmware(fw_id)
            self._log(msg)

            # Also remove from Git release
            self.git.remove_from_index([fw_id])

        self._refresh_firmware_list()

    def _choose_sd_path(self):
        path = filedialog.askdirectory(title="Select SD Card")
        if path:
            self.sd_path_var.set(path)
            self.sd.set_path(path)
            self._load_sd_card()

    def _load_sd_card(self):
        """Load SD card contents."""
        self.sd_listbox.delete(0, tk.END)

        if not self.sd.set_path(self.sd_path_var.get()):
            messagebox.showerror("Error", "Invalid SD card path")
            return

        success, msg = self.sd.load_index()
        self._log(msg)

        for fw_id in self.sd.get_firmware_list():
            self.sd_listbox.insert(tk.END, fw_id)

    def _on_sd_select(self, event=None):
        """Show SD firmware details in structured panel."""
        sel = self.sd_listbox.curselection()
        if not sel:
            return

        fw_id = self.sd_listbox.get(sel[0])
        meta = self.sd.get_firmware_meta(fw_id)

        if not meta:
            self._det_clear()
            return

        device = meta.get('device_type', '')
        version = meta.get('version', '')
        self._det_name_var.set(f"{device} {version}".strip() or fw_id)
        self._det_ver_badge.set_text(version)
        self._det_ver_badge.set_status("info")
        self._det_type_var.set(f"Device: {device}" if device else "SD Card")
        self._det_desc_var.set(meta.get('description', '') or 'SD Card firmware')
        self._det_id_var.set(fw_id)
        self._det_path_var.set(f"{self.sd_path_var.get()}/{fw_id}")
        self._det_md5_var.set(meta.get('md5', ''))
        self._det_size_var.set("\u2014")
        self._det_date_var.set("\u2014")

    # ==================== Flash History ====================

    HISTORY_FILE = os.path.join(APP_DIR, "flash_history.json")
    HISTORY_MAX = 50

    def _history_load(self) -> list:
        """Load flash history from local JSON file."""
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
        """Save flash history to local JSON file."""
        try:
            with open(self.HISTORY_FILE, "w", encoding="utf-8") as f:
                json.dump(self._flash_history[-self.HISTORY_MAX:], f, indent=2)
        except Exception:
            pass

    def _history_add(self, fw_id: str, fw: dict, sd_path: str, mode: str):
        """Add an entry to flash history."""
        from datetime import datetime
        entry = {
            "fw_id": fw_id,
            "device_type": fw.get("device_type", ""),
            "version": fw.get("version", ""),
            "sd_path": sd_path,
            "mode": mode,
            "time": datetime.now().strftime("%Y-%m-%d %H:%M")
        }
        self._flash_history.append(entry)
        self._history_save()
        self._history_refresh_listbox()

    def _history_refresh_listbox(self):
        """Refresh the history listbox (most recent first)."""
        self._hist_listbox.delete(0, tk.END)
        for entry in reversed(self._flash_history):
            device = entry.get("device_type", "?")
            version = entry.get("version", "?")
            mode = entry.get("mode", "?")
            fw_id = entry.get("fw_id", "?")
            tag = "P" if mode == "plain" else "E"
            line = f"[{tag}] {fw_id}  {device} v{version}"
            self._hist_listbox.insert(tk.END, line)

    def _on_hist_select(self, event=None):
        """Show history entry details in the detail panel."""
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

    def _sd_save_ordered(self):
        """Apply OLED order to SD index, then save."""
        oled_order = self.settings.get("oled_config", {}).get("order", [])
        if oled_order:
            self.sd.reorder_index(oled_order)
        self._sd_save_ordered()

    def _get_encryptor(self) -> Optional[FWEncryptor]:
        """Get encryptor with validation."""
        key = self.enc_key.get()
        iv = self.enc_iv.get()

        valid, msg = validate_key_iv(key, iv)
        if not valid:
            messagebox.showerror("Invalid Key/IV", msg)
            return None

        if not load_crypto():
            messagebox.showerror("Error", "Crypto library not available.\nInstall: pip install pycryptodome")
            return None

        try:
            return FWEncryptor(key, iv)
        except Exception as e:
            messagebox.showerror("Error", str(e))
            return None

    def _copy_to_sd_plain(self):
        """Copy selected firmware to SD card (plain)."""
        fw_ids = self._get_selected_fw_ids()
        if not fw_ids:
            return messagebox.showinfo("Info", "Select firmware(s) in tree to copy")

        if not self.sd.set_path(self.sd_path_var.get()):
            return messagebox.showerror("Error", "Invalid SD card path")

        if not messagebox.askyesno("Confirm", f"Copy {len(fw_ids)} firmware(s) to SD card (plain)?"):
            return

        count = 0
        for fw_id in fw_ids:
            fw = self.lib.get_firmware(fw_id)
            if not fw:
                continue

            success, msg = self.sd.copy_plain(
                fw_id,
                fw["_path"],
                fw,
                progress_callback=self._log
            )
            if success:
                count += 1
                self._history_add(fw_id, fw, self.sd_path_var.get(), "plain")
            self._log(msg)

        self._sd_save_ordered()
        self._load_sd_card()
        messagebox.showinfo("Done", f"Copied {count} firmware(s) to SD card")

    def _copy_to_sd_enc(self):
        """Copy selected firmware to SD card (encrypted)."""
        fw_ids = self._get_selected_fw_ids()
        if not fw_ids:
            return messagebox.showinfo("Info", "Select firmware(s) in tree to copy")

        if not self.sd.set_path(self.sd_path_var.get()):
            return messagebox.showerror("Error", "Invalid SD card path")

        encryptor = self._get_encryptor()
        if not encryptor:
            return

        if not messagebox.askyesno("Confirm", f"Copy {len(fw_ids)} firmware(s) to SD card (encrypted)?"):
            return

        count = 0
        for fw_id in fw_ids:
            fw = self.lib.get_firmware(fw_id)
            if not fw:
                continue

            success, msg = self.sd.copy_encrypted(
                fw_id,
                fw["_path"],
                fw,
                encryptor,
                progress_callback=self._log
            )
            if success:
                count += 1
                self._history_add(fw_id, fw, self.sd_path_var.get(), "encrypted")
            self._log(msg)

        self._sd_save_ordered()
        self._load_sd_card()
        messagebox.showinfo("Done", f"Copied {count} firmware(s) to SD card (encrypted)")

    def _remove_from_sd(self):
        """Remove selected firmware from SD card."""
        sel = self.sd_listbox.curselection()
        if not sel:
            return messagebox.showinfo("Info", "Select firmware from SD card list")

        fw_id = self.sd_listbox.get(sel[0])

        if not messagebox.askyesno("Confirm", f"Remove '{fw_id}' from SD card?"):
            return

        success, msg = self.sd.remove_firmware(fw_id)
        self._log(msg)

        if success:
            self._sd_save_ordered()
            self._load_sd_card()

    def _sync_to_git(self):
        """Export and push all firmware to Git with single confirmation."""
        if not self.git.is_git_installed():
            return self._prompt_git_install()

        url = self.git_url_var.get().strip()
        if not url:
            return messagebox.showerror("Error", "Git repository URL not set.\nGo to Settings tab.")

        # Get all firmware
        all_fw = self.lib.list_firmwares()
        if not all_fw:
            return messagebox.showinfo("Info", "No firmware in library")

        # Single confirmation
        if not messagebox.askyesno("Sync to Git",
            f"Export and push {len(all_fw)} firmware(s) to Git?\n\n"
            f"Repo: {url}"):
            return

        # Check if any ESP32 firmware needs encryption
        has_esp32 = any("STM32" not in fw.get("device_type", "").upper() for fw in all_fw)
        encryptor = None
        if has_esp32:
            encryptor = self._get_encryptor()
            if not encryptor:
                return

        self.git.set_repo_url(url)

        # Export all
        exported = []
        for fw in all_fw:
            fw_id = fw.get("fw_id")
            success, result = self.git.export_firmware(
                fw_id,
                fw["_path"],
                fw,
                encryptor,
                progress_callback=self._log
            )
            if success:
                exported.append(result)

        if exported:
            # Apply OLED order to git index (same as SD card)
            oled_order = self.settings.get("oled_config", {}).get("order", [])
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
            self.git.update_index(exported)
            self._log(f"Exported {len(exported)} firmware(s)")

        # Push via script (opens terminal)
        try:
            script = self.git.create_push_script()
            os.startfile(script)
            self._log("Git sync started...")
        except Exception as e:
            messagebox.showerror("Error", str(e))


# ==================== Entry Point ====================

if __name__ == "__main__":
    # Need simpledialog for reset
    import tkinter.simpledialog

    app = MainApp()
    app.mainloop()
