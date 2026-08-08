"""
netflash.py
===========
NetFlash tab — master-slave architecture.

Layout:
    [Master Bar]     IP + Connect + Find + connection status
    [Hero Actions]   ⚡ FLASH ALL · ✖ Erase All · ↻ Reboot All · summary
    [Slave Cards]    Auto-reflow grid (min 320px), 1 card per slave

Each slave card shows:
    label + I2C addr + target chip + status + FW dropdown + progress + mini log
    border color: green=online, gray=offline, red=last fail

State polling: Tkinter `after()` loop calling MasterClient.get_status() every
500ms when any slave is busy, else 1000ms.
"""

from __future__ import annotations

import threading
import tkinter as tk
from collections import deque
from tkinter import messagebox, simpledialog, ttk
from typing import Optional

from modules.master_client import MasterClient, SlaveInfo, SlaveStatus, SyncStatus
from modules.net_flash import discover_nodes
from modules.theme import Colors, Fonts


SYNC_ACTIVE_PHASES = ("connecting", "downloading", "verifying", "storing")


CARD_MIN_W = 320
POLL_INTERVAL_BUSY_MS = 500
POLL_INTERVAL_IDLE_MS = 1000
MINI_LOG_LINES = 3


class NetFlashTab(ttk.Frame):
    """NetFlash master-slave control surface."""

    def __init__(self, parent, app, master_client: MasterClient):
        super().__init__(parent, padding=10)
        self.app = app
        # NOTE: do NOT use `self.master` — that name is reserved by Tk for parent widget.
        self.master_client = master_client

        self._cards: dict[int, dict] = {}    # addr → {widgets, vars, state}
        self._poll_id: Optional[str] = None
        self._session_ok = 0
        self._session_fail = 0
        self._flash_in_progress = False
        self._sync_in_progress = False

        self.pack(fill=tk.BOTH, expand=True)
        self._build()

        master_ip = app.settings.get("master_ip", "").strip()
        backend = app.settings.get("netflash_backend", "mock")
        if backend == "mock":
            # Mock backend: auto-connect on launch so demo slaves render
            # without requiring LAN discovery.
            demo_ip = master_ip or "mock://demo"
            self.after(300, lambda: self._connect(demo_ip))
        elif master_ip:
            self.after(300, lambda: self._connect(master_ip))
        else:
            self.after(800, self._find_master)

    # ─────────────────────────── Layout ───────────────────────────

    def _build(self):
        self.columnconfigure(0, weight=1)
        self.rowconfigure(1, weight=1)

        self._build_master_bar()  # row 0 — full width

        # Row 1: horizontal split (Master Control on left, Cards Grid on right)
        pw = ttk.PanedWindow(self, orient="horizontal")
        pw.grid(row=1, column=0, sticky="nsew")

        left_wrap = ttk.Frame(pw)
        pw.add(left_wrap, weight=30)
        self._build_master_control(left_wrap)

        right_wrap = ttk.Frame(pw)
        pw.add(right_wrap, weight=70)
        self._build_cards_area(right_wrap)

    def _build_master_bar(self):
        bar = ttk.Frame(self)
        bar.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        bar.columnconfigure(1, weight=1)

        ttk.Label(bar, text="Master:").grid(row=0, column=0, padx=(0, 6))

        self.master_ip_var = tk.StringVar(value=self.app.settings.get("master_ip", ""))
        ip_entry = ttk.Entry(bar, textvariable=self.master_ip_var,
                             font=Fonts.mono_small(), width=22)
        ip_entry.grid(row=0, column=1, sticky="ew", padx=(0, 6))
        ip_entry.bind("<Return>", lambda e: self._connect(self.master_ip_var.get().strip()))

        self.btn_connect = ttk.Button(bar, text="Connect", style="Primary.TButton",
                                       command=lambda: self._connect(self.master_ip_var.get().strip()))
        self.btn_connect.grid(row=0, column=2, padx=(0, 6))

        self.btn_find = ttk.Button(bar, text="🔍 Find", style="Secondary.TButton",
                                    command=self._find_master)
        self.btn_find.grid(row=0, column=3, padx=(0, 12))

        self.conn_status_var = tk.StringVar(value="◯ Not connected")
        self.lbl_conn_status = tk.Label(bar, textvariable=self.conn_status_var,
                                          fg=Colors.TEXT_MUTED, bg=Colors.BG_DARK,
                                          font=Fonts.bold())
        self.lbl_conn_status.grid(row=0, column=4, sticky="e")

    def _build_master_control(self, parent):
        """Left column: Sync Library → Apply FW + FLASH ALL → Erase/Reboot → status."""
        panel = tk.Frame(parent, bg=Colors.BG_CARD, padx=18, pady=18)
        panel.pack(fill=tk.BOTH, expand=True)
        panel.columnconfigure(0, weight=1)
        panel.columnconfigure(1, weight=1)

        # ── Library Sync — slaves mirror the full server FW library ──
        tk.Label(panel, text="Library Sync", bg=Colors.BG_CARD,
                 fg=Colors.PRIMARY, font=Fonts.bold()).grid(
            row=0, column=0, columnspan=2, sticky="w")
        tk.Label(panel,
                 text="Each slave pulls the full FW library over its own WiFi.",
                 bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
                 font=Fonts.small()).grid(row=1, column=0, columnspan=2,
                                           sticky="w", pady=(0, 6))

        # Emerald fill + white text — clearly secondary to the violet
        # FLASH ALL hero below.
        self.btn_sync_all = tk.Button(
            panel, text="⟲ Sync Library to All",
            font=Fonts.bold(),
            bg=Colors.SUCCESS, fg=Colors.TEXT_DARK,
            activebackground="#34D399", activeforeground=Colors.TEXT_DARK,
            relief="flat", borderwidth=0, pady=12,
            cursor="hand2",
            command=self._sync_all,
            state="disabled",
        )
        self.btn_sync_all.grid(row=2, column=0, columnspan=2, sticky="ew")

        self.sync_summary_var = tk.StringVar(value="—")
        tk.Label(panel, textvariable=self.sync_summary_var,
                 bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
                 font=Fonts.small()).grid(row=3, column=0, columnspan=2,
                                           sticky="w", pady=(4, 0))

        tk.Frame(panel, height=1, bg=Colors.BORDER).grid(
            row=4, column=0, columnspan=2, sticky="ew", pady=(14, 12))

        # ── Apply FW to all (header + dropdown) ──
        tk.Label(panel, text="Apply FW to all online:",
                 bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
                 font=Fonts.normal()).grid(row=5, column=0, columnspan=2,
                                            sticky="w")

        self.global_fw_combo = ttk.Combobox(panel, state="disabled",
                                             font=Fonts.mono_small())
        self.global_fw_combo.grid(row=6, column=0, columnspan=2,
                                   sticky="ew", pady=(2, 16))
        self.global_fw_combo.bind("<<ComboboxSelected>>",
                                   self._on_global_fw_selected)
        self.global_fw_combo.set("(connect master to load FW list)")
        self._global_fw_ids: list[str] = []
        # None = per-card mode (Flash All uses each card's own dropdown).
        # Any FW id = global mode (Flash All targets only slaves with that FW;
        # others SKIPPED so the user never gets surprised by a stale per-card
        # selection silently re-flashing.
        self._global_fw_selected_id: Optional[str] = None

        # --- FLASH ALL — biggest, primary focus ---
        self.btn_flash_all = tk.Button(
            panel, text="⚡ FLASH ALL",
            font=(Fonts.FAMILY, 18, "bold"),
            bg=Colors.PRIMARY, fg=Colors.TEXT_DARK,
            activeforeground=Colors.TEXT_DARK,
            relief="flat", borderwidth=0,
            pady=22,
            cursor="hand2",
            command=self._flash_all,
            state="disabled",
        )
        self.btn_flash_all.grid(row=7, column=0, columnspan=2,
                                 sticky="ew", pady=(0, 8))

        # --- Erase / Reboot — side-by-side secondary buttons ---
        self.btn_erase_all = tk.Button(
            panel, text="✖ Erase All",
            font=Fonts.bold(),
            bg=Colors.ERROR, fg=Colors.TEXT_DARK,
            relief="flat", borderwidth=0,
            pady=10,
            cursor="hand2",
            command=self._erase_all,
            state="disabled",
        )
        self.btn_erase_all.grid(row=8, column=0, sticky="ew", padx=(0, 4))

        self.btn_reboot_all = tk.Button(
            panel, text="↻ Reboot All",
            font=Fonts.bold(),
            bg=Colors.BG_DARK, fg=Colors.TEXT,
            relief="flat", borderwidth=1,
            pady=10,
            cursor="hand2",
            command=self._reboot_all,
            state="disabled",
        )
        self.btn_reboot_all.grid(row=8, column=1, sticky="ew", padx=(4, 0))

        # --- Divider ---
        tk.Frame(panel, height=1, bg=Colors.BORDER).grid(
            row=9, column=0, columnspan=2, sticky="ew", pady=(18, 12))

        # --- Status summary — multi-line, big numbers on right ---
        tk.Label(panel, text="Status", bg=Colors.BG_CARD,
                 fg=Colors.PRIMARY, font=Fonts.bold()).grid(
            row=10, column=0, columnspan=2, sticky="w", pady=(0, 4))

        self.status_online_var = tk.StringVar(value="—")
        self.status_ok_var = tk.StringVar(value="—")
        self.status_fail_var = tk.StringVar(value="—")

        def stat_row(r, lbl_text, var, value_color):
            tk.Label(panel, text=lbl_text, bg=Colors.BG_CARD,
                     fg=Colors.TEXT_MUTED, font=Fonts.normal()).grid(
                row=r, column=0, sticky="w", pady=2)
            tk.Label(panel, textvariable=var, bg=Colors.BG_CARD,
                     fg=value_color,
                     font=(Fonts.FAMILY, 13, "bold"), anchor="e").grid(
                row=r, column=1, sticky="e", pady=2)

        stat_row(11, "Online", self.status_online_var, Colors.TEXT)
        stat_row(12, "Success", self.status_ok_var, Colors.SUCCESS)
        stat_row(13, "Failed", self.status_fail_var, Colors.ERROR)

        # Legacy compatibility — kept so _update_summary() can still set it
        # if anything reads it. Not displayed in the new layout.
        self.summary_var = tk.StringVar(value="")

    def _build_cards_area(self, parent):
        container = ttk.Frame(parent)
        container.pack(fill=tk.BOTH, expand=True)
        container.columnconfigure(0, weight=1)
        container.rowconfigure(0, weight=1)

        self.canvas = tk.Canvas(container, bg=Colors.BG_DARK,
                                highlightthickness=0, borderwidth=0)
        self.canvas.grid(row=0, column=0, sticky="nsew")

        vscroll = ttk.Scrollbar(container, orient="vertical",
                                command=self.canvas.yview)
        vscroll.grid(row=0, column=1, sticky="ns")
        self.canvas.configure(yscrollcommand=vscroll.set)

        self.cards_frame = tk.Frame(self.canvas, bg=Colors.BG_DARK)
        self.canvas_window = self.canvas.create_window(
            (0, 0), window=self.cards_frame, anchor="nw"
        )
        self.cards_frame.bind(
            "<Configure>",
            lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all"))
        )
        self.canvas.bind("<Configure>", self._on_canvas_configure)
        self.canvas.bind("<Enter>",
                         lambda e: self.canvas.bind_all("<MouseWheel>", self._on_mousewheel))
        self.canvas.bind("<Leave>",
                         lambda e: self.canvas.unbind_all("<MouseWheel>"))

        self.empty_lbl = tk.Label(
            self.cards_frame,
            text="No master connected.\nEnter master IP above or click 🔍 Find.",
            fg=Colors.TEXT_MUTED, bg=Colors.BG_DARK,
            font=Fonts.normal(), justify="center",
        )
        self.empty_lbl.grid(row=0, column=0, padx=40, pady=60)

    # ─────────────────────────── Connection ───────────────────────────

    def _connect(self, host: str):
        if not host:
            return
        self.btn_connect.config(state="disabled", text="Connecting...")
        self.conn_status_var.set("◯ Connecting...")
        self.lbl_conn_status.configure(fg=Colors.TEXT_MUTED)
        threading.Thread(
            target=self._do_connect, args=(host,), daemon=True
        ).start()

    def _do_connect(self, host: str):
        try:
            result = self.master_client.connect(host)
            self.after(0, self._on_connected, host, result, None)
        except Exception as e:
            self.after(0, self._on_connected, host, None, str(e))

    def _on_connected(self, host: str, result: Optional[dict], err: Optional[str]):
        self.btn_connect.config(state="normal", text="Connect")
        if err or not result or not result.get("ok"):
            self.conn_status_var.set(f"✗ Connect failed: {err or 'unknown'}")
            self.lbl_conn_status.configure(fg=Colors.ERROR)
            self.app._log(f"NetFlash: connect to {host} failed — {err}")
            return

        self.conn_status_var.set(f"● Connected · {result.get('slaves_count', 0)} slaves")
        self.lbl_conn_status.configure(fg=Colors.SUCCESS)
        self.app._log(f"NetFlash: connected to {host} ({result.get('master_id')})")

        # Persist IP
        self.app.settings["master_ip"] = host
        self._persist_settings()

        # Enable hero actions
        self.btn_sync_all.config(state="normal")
        self.btn_flash_all.config(state="normal")
        self.btn_erase_all.config(state="normal")
        self.btn_reboot_all.config(state="normal")

        # Render slaves
        slaves = self.master_client.get_slaves()
        self._render_cards(slaves)
        self._build_global_fw_list(slaves)
        self._refresh_sync_indicators()
        self._start_polling()

    def _find_master(self):
        self.btn_find.config(state="disabled", text="Finding...")
        self.app._log("NetFlash: scanning LAN for master...")
        threading.Thread(target=self._do_find_master, daemon=True).start()

    def _do_find_master(self):
        try:
            hosts = discover_nodes(timeout=5.0)
            self.after(0, self._on_find_done, hosts, None)
        except Exception as e:
            self.after(0, self._on_find_done, [], str(e))

    def _on_find_done(self, hosts: list, err: Optional[str]):
        self.btn_find.config(state="normal", text="🔍 Find")
        if err:
            messagebox.showerror("Find Master", f"Scan error: {err}")
            return
        if not hosts:
            messagebox.showinfo("Find Master",
                                 "No master found on LAN.\nEnter IP manually.")
            return
        # Take first hit (in master-slave model there should be only 1)
        host = hosts[0]
        self.master_ip_var.set(host)
        self._connect(host)

    # ─────────────────────────── Cards ───────────────────────────

    def _render_cards(self, slaves: list[SlaveInfo]):
        # Clear old
        for c in self._cards.values():
            c["card_frame"].destroy()
        self._cards.clear()
        self.empty_lbl.grid_forget()

        labels_override = self.app.settings.get("slave_labels", {})
        for slave in slaves:
            key = f"0x{slave.addr:02x}"
            label = labels_override.get(key, slave.label)
            slave.label = label
            self._build_card(slave)

        self._reflow_cards()
        self._update_summary()

    def _build_card(self, slave: SlaveInfo):
        cf = tk.Frame(self.cards_frame, bg=Colors.BG_CARD,
                      highlightbackground=Colors.BORDER,
                      highlightthickness=1)

        ct = tk.Frame(cf, bg=Colors.BG_CARD, padx=10, pady=8)
        ct.pack(fill=tk.BOTH, expand=True)
        ct.columnconfigure(1, weight=1)

        # Row 0: dot + label + buttons
        dot = tk.Label(ct, text="●", bg=Colors.BG_CARD,
                       fg=Colors.TEXT_MUTED, font=(Fonts.FAMILY, 11))
        dot.grid(row=0, column=0, sticky="w", padx=(0, 6))

        label_var = tk.StringVar(value=slave.label)
        tk.Label(ct, textvariable=label_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=(Fonts.FAMILY, 11, "bold"),
                 anchor="w").grid(row=0, column=1, sticky="ew")

        btn_box = tk.Frame(ct, bg=Colors.BG_CARD)
        btn_box.grid(row=0, column=2, sticky="ne")
        ttk.Button(btn_box, text="↻", width=2, style="Icon.TButton",
                   command=lambda a=slave.addr: self._ping_slave(a)).pack(side=tk.LEFT, padx=(0, 2))
        ttk.Button(btn_box, text="✎", width=2, style="Icon.TButton",
                   command=lambda a=slave.addr: self._edit_label(a)).pack(side=tk.LEFT)

        # Row 1: addr · target
        meta_var = tk.StringVar(
            value=f"0x{slave.addr:02x} · {slave.target_type or 'no target'}"
        )
        tk.Label(ct, textvariable=meta_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT_MUTED, font=Fonts.mono_small(),
                 anchor="w").grid(row=1, column=0, columnspan=3, sticky="ew", pady=(2, 6))

        # Row 2: status text
        status_var = tk.StringVar(
            value="Ready" if slave.online else "Offline"
        )
        tk.Label(ct, textvariable=status_var, bg=Colors.BG_CARD,
                 fg=Colors.TEXT, font=Fonts.bold(),
                 anchor="w").grid(row=2, column=0, columnspan=3, sticky="ew")

        # Row 3: library sync indicator
        sync_var = tk.StringVar(value="Library: —")
        sync_label = tk.Label(ct, textvariable=sync_var, bg=Colors.BG_CARD,
                              fg=Colors.TEXT_MUTED, font=Fonts.small(),
                              anchor="w")
        sync_label.grid(row=3, column=0, columnspan=3, sticky="ew", pady=(2, 0))

        # Row 4: FW dropdown (FW versions stored on this slave)
        fw_list = self.master_client.get_slave_fw_list(slave.addr)
        fw_values = [f["display"] for f in fw_list]
        fw_combo = ttk.Combobox(ct, values=fw_values, state="readonly",
                                font=Fonts.mono_small())
        fw_combo.grid(row=4, column=0, columnspan=3, sticky="ew", pady=(6, 4))
        if fw_values:
            fw_combo.current(0)
        else:
            fw_combo.set("(sync library first)")
            fw_combo.config(state="disabled")

        # Row 5: progress
        progress = ttk.Progressbar(ct, mode="determinate", maximum=100, length=200)
        progress.grid(row=5, column=0, columnspan=3, sticky="ew", pady=4)

        # Row 6: action buttons
        actions = tk.Frame(ct, bg=Colors.BG_CARD)
        actions.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(4, 6))
        flash_btn = ttk.Button(actions, text="⚡ Flash", style="Primary.TButton",
                               command=lambda a=slave.addr: self._flash_slave(a))
        flash_btn.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4))
        reboot_btn = ttk.Button(actions, text="↻ Reboot", style="Secondary.TButton",
                                command=lambda a=slave.addr: self._reboot_slave(a))
        reboot_btn.pack(side=tk.LEFT, fill=tk.X, expand=True)

        # Row 7: mini log
        mini_log = tk.Text(ct, height=MINI_LOG_LINES,
                           bg=Colors.BG_INPUT, fg=Colors.TEXT,
                           font=Fonts.mono_small(),
                           wrap="word", relief="flat", borderwidth=0,
                           state="disabled")
        mini_log.grid(row=7, column=0, columnspan=3, sticky="ew", pady=(2, 0))

        self._cards[slave.addr] = {
            "card_frame": cf,
            "dot": dot,
            "label_var": label_var,
            "meta_var": meta_var,
            "status_var": status_var,
            "sync_var": sync_var,
            "sync_label": sync_label,
            "sync_phase": "idle",
            "fw_combo": fw_combo,
            "fw_list": fw_list,
            "progress": progress,
            "flash_btn": flash_btn,
            "reboot_btn": reboot_btn,
            "mini_log": mini_log,
            "log_lines": deque(maxlen=MINI_LOG_LINES),
            "online": slave.online,
            "last_result": slave.last_result,
            "target_type": slave.target_type,
        }

        self._apply_card_visual(slave.addr)
        if slave.last_result == "ok" and slave.last_duration_s:
            self._card_log(slave.addr, f"Last: {slave.current_fw} OK ({slave.last_duration_s:.1f}s)")
        elif slave.last_result == "fail":
            self._card_log(slave.addr, f"Last: ✗ {slave.error_text or 'fail'}")

    def _apply_card_visual(self, addr: int):
        c = self._cards[addr]
        cf = c["card_frame"]
        if not c["online"]:
            cf.configure(highlightbackground=Colors.BORDER, highlightthickness=1)
            c["dot"].configure(fg=Colors.TEXT_MUTED)
            c["flash_btn"].config(state="disabled")
            c["reboot_btn"].config(state="disabled")
            c["fw_combo"].config(state="disabled")
        else:
            failed = c["last_result"] == "fail"
            cf.configure(
                highlightbackground=Colors.ERROR if failed else Colors.SUCCESS,
                highlightthickness=2)
            c["dot"].configure(fg=Colors.ERROR if failed else Colors.SUCCESS)
            c["reboot_btn"].config(state="normal")
            # Flash needs a stored FW (post-sync) and a real target.
            can_flash = bool(c["fw_list"]) and c["target_type"] != "no-target"
            c["flash_btn"].config(state="normal" if can_flash else "disabled")
            c["fw_combo"].config(
                state="readonly" if c["fw_list"] else "disabled")

    def _on_canvas_configure(self, event):
        self.canvas.itemconfig(self.canvas_window, width=event.width)
        self._reflow_cards()

    def _on_mousewheel(self, event):
        self.canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

    def _reflow_cards(self):
        if not self._cards:
            return
        canvas_w = self.canvas.winfo_width()
        if canvas_w < 20:
            canvas_w = 1280
        num_cols = max(1, canvas_w // CARD_MIN_W)

        for i, addr in enumerate(self._cards):
            r, c = divmod(i, num_cols)
            self._cards[addr]["card_frame"].grid(
                row=r, column=c, padx=4, pady=4, sticky="nsew"
            )
        for c in range(num_cols):
            self.cards_frame.columnconfigure(c, weight=1, uniform="card")
        for c in range(num_cols, num_cols + 10):
            self.cards_frame.columnconfigure(c, weight=0, uniform="")

    def _card_log(self, addr: int, msg: str):
        c = self._cards.get(addr)
        if c is None:
            return
        c["log_lines"].append(msg)
        widget = c["mini_log"]
        widget.config(state="normal")
        widget.delete("1.0", tk.END)
        widget.insert(tk.END, "\n".join(c["log_lines"]))
        widget.config(state="disabled")

    # ─────────────────────────── Polling ───────────────────────────

    def _start_polling(self):
        self._stop_polling()
        self._tick_poll()

    def _stop_polling(self):
        if self._poll_id:
            self.after_cancel(self._poll_id)
            self._poll_id = None

    def _tick_poll(self):
        try:
            statuses = self.master_client.get_status()
        except Exception as e:
            self.app._log(f"NetFlash: poll error — {e}")
            statuses = []

        # Detect slaves that appeared after the initial render — e.g. a board
        # freshly provisioned via the Phost Setup tab — and re-render so they
        # show up without forcing a reconnect.
        new_addrs = {st.addr for st in statuses} - set(self._cards)
        if new_addrs and not self._flash_in_progress and not self._sync_in_progress:
            slaves = self.master_client.get_slaves()
            self._render_cards(slaves)
            self._build_global_fw_list(slaves)
            self._refresh_sync_indicators()
            self.app._log(f"NetFlash: {len(new_addrs)} new slave(s) detected")

        any_busy = False
        for st in statuses:
            if st.addr in self._cards:
                self._apply_status(st)
                if st.busy:
                    any_busy = True

        # Library-sync polling — only while a sync job is running
        sync_active = False
        if self._sync_in_progress:
            try:
                sync_statuses = self.master_client.get_sync_status()
            except Exception as e:
                self.app._log(f"NetFlash: sync poll error — {e}")
                sync_statuses = []
            for sst in sync_statuses:
                if sst.addr not in self._cards:
                    continue
                prev_phase = self._cards[sst.addr]["sync_phase"]
                self._apply_sync_status(sst)
                if sst.phase in SYNC_ACTIVE_PHASES:
                    sync_active = True
                # On transition into a finished state, refresh the FW dropdown
                # so newly-downloaded versions become selectable.
                if prev_phase != sst.phase and sst.phase in ("done", "failed"):
                    self._refresh_card_fw(sst.addr)
                    if sst.phase == "done":
                        self._card_log(sst.addr,
                                       f"✓ Library synced ({sst.files_done}/{sst.files_total})")
                    else:
                        self._card_log(sst.addr, f"✗ Sync: {sst.error or 'failed'}")
            self._update_sync_summary()
            if not sync_active:
                self._sync_in_progress = False
                # Rebuild global FW union now that pools may have grown
                self._build_global_fw_list(self.master_client.get_slaves())

        # Pick up finalized slave info (current_fw, last_result) when flash finishes
        if self._flash_in_progress:
            for slave in self.master_client.get_slaves():
                if slave.addr in self._cards:
                    c = self._cards[slave.addr]
                    if c["last_result"] != slave.last_result:
                        c["last_result"] = slave.last_result
                        if slave.last_result == "ok":
                            self._session_ok += 1
                            self._card_log(slave.addr,
                                           f"✓ {slave.current_fw} OK ({slave.last_duration_s:.1f}s)")
                        elif slave.last_result == "fail":
                            self._session_fail += 1
                            self._card_log(slave.addr,
                                           f"✗ {slave.error_text or 'fail'}")
                        self._apply_card_visual(slave.addr)
                        self._update_summary()

            if not any_busy:
                self._flash_in_progress = False
                self._show_flash_all_toast()

        self._update_summary()
        interval = (POLL_INTERVAL_BUSY_MS if (any_busy or sync_active)
                    else POLL_INTERVAL_IDLE_MS)
        self._poll_id = self.after(interval, self._tick_poll)

    def _apply_status(self, st: SlaveStatus):
        c = self._cards[st.addr]
        c["progress"]["value"] = st.progress
        c["status_var"].set(st.status_text)

    def _update_summary(self):
        total = len(self._cards)
        online = sum(1 for c in self._cards.values() if c["online"])
        self.status_online_var.set(f"{online}/{total}" if total else "—")
        self.status_ok_var.set(str(self._session_ok) if (self._session_ok or self._session_fail) else "—")
        self.status_fail_var.set(str(self._session_fail) if (self._session_ok or self._session_fail) else "—")

    # ─────────────────────────── Library Sync ───────────────────────────

    def _sync_all(self):
        online = [a for a, c in self._cards.items() if c["online"]]
        if not online:
            messagebox.showinfo("Sync Library", "No online slaves to sync.")
            return
        # Real backend pulls from a Git raw URL; mock ignores it.
        manifest_url = (self.app.settings.get("manifest_url", "").strip()
                        or "mock://manifest.json")
        if not messagebox.askyesno(
            "Confirm Library Sync",
            f"Sync the full FW library to {len(online)} online slave(s)?\n\n"
            "Each slave downloads any missing firmware over its own WiFi. "
            "Already up-to-date slaves are a no-op."
        ):
            return
        self._sync_in_progress = True
        for addr in online:
            self._card_log(addr, "Library sync started...")
        result = self.master_client.sync_library(online, manifest_url)
        for addr in result.get("skipped", []):
            if addr in self._cards:
                self._card_log(addr, "⊘ sync skipped (offline/busy)")
        self._update_sync_summary()

    def _apply_sync_status(self, st: SyncStatus):
        c = self._cards.get(st.addr)
        if c is None:
            return
        c["sync_phase"] = st.phase
        var, lbl = c["sync_var"], c["sync_label"]
        n = f"{st.files_done}/{st.files_total}"
        if st.phase in SYNC_ACTIVE_PHASES:
            cur = st.current_file or "..."
            var.set(f"⟳ {st.phase}: {cur}  ({n})")
            lbl.configure(fg=Colors.WARNING)
        elif st.phase == "failed":
            var.set(f"✗ Sync failed: {st.error or 'error'}")
            lbl.configure(fg=Colors.ERROR)
        elif st.files_done >= st.files_total and st.files_total > 0:
            var.set(f"Library: {n} ✓")
            lbl.configure(fg=Colors.SUCCESS)
        else:
            var.set(f"Library: {n}")
            lbl.configure(fg=Colors.TEXT_MUTED)

    def _refresh_sync_indicators(self):
        """One-shot pull of sync status to paint card library badges + summary."""
        try:
            for st in self.master_client.get_sync_status():
                self._apply_sync_status(st)
        except Exception as e:
            self.app._log(f"NetFlash: sync status error — {e}")
        self._update_sync_summary()

    def _update_sync_summary(self):
        try:
            statuses = self.master_client.get_sync_status()
        except Exception:
            statuses = []
        online = {a for a, c in self._cards.items() if c["online"]}
        rel = [s for s in statuses if s.addr in online]
        if not rel:
            self.sync_summary_var.set("—")
            return
        synced = sum(1 for s in rel
                     if s.files_total > 0 and s.files_done >= s.files_total
                     and s.phase not in ("failed",) + SYNC_ACTIVE_PHASES)
        syncing = sum(1 for s in rel if s.phase in SYNC_ACTIVE_PHASES)
        failed = sum(1 for s in rel if s.phase == "failed")
        parts = [f"{synced}/{len(rel)} fully synced"]
        if syncing:
            parts.append(f"{syncing} syncing")
        if failed:
            parts.append(f"{failed} failed")
        self.sync_summary_var.set(" · ".join(parts))

    def _refresh_card_fw(self, addr: int):
        """Re-pull a slave's stored FW list into its dropdown (post-sync)."""
        c = self._cards.get(addr)
        if c is None:
            return
        fw_list = self.master_client.get_slave_fw_list(addr)
        c["fw_list"] = fw_list
        values = [f["display"] for f in fw_list]
        if values:
            keep = c["fw_combo"].get()
            c["fw_combo"].config(values=values, state="readonly")
            if keep in values:
                c["fw_combo"].set(keep)
            elif c["fw_combo"].current() < 0:
                c["fw_combo"].current(0)
        else:
            c["fw_combo"].set("(sync library first)")
            c["fw_combo"].config(state="disabled")
        self._apply_card_visual(addr)

    # ─────────────────────────── Actions ───────────────────────────

    def _build_global_fw_list(self, slaves: list[SlaveInfo]):
        """Compute union of FW ids across all slave pools and populate the
        global FW dropdown. Each entry shows the FW display plus a count of
        how many slaves can flash it: 'EMC32 v1.2  (3/4)'."""
        from collections import OrderedDict
        union: OrderedDict[str, dict] = OrderedDict()
        for slave in slaves:
            for fw in self.master_client.get_slave_fw_list(slave.addr):
                fid = fw["id"]
                if fid not in union:
                    union[fid] = {"id": fid, "display": fw["display"], "count": 0}
                union[fid]["count"] += 1

        total = len(slaves)
        # First entry is a sentinel: clears global mode so Flash All falls
        # back to each card's own per-card selection.
        self.PER_CARD_SENTINEL = "(per-card selection)"
        self._global_fw_ids = [None] + list(union.keys())
        values = [self.PER_CARD_SENTINEL] + [
            f"{f['display']}  ({f['count']}/{total})"
            for f in union.values()
        ]

        if len(values) > 1:
            self.global_fw_combo.config(state="readonly", values=values)
            self.global_fw_combo.current(0)
            self._global_fw_selected_id = None
        else:
            self.global_fw_combo.config(state="disabled", values=[])
            self.global_fw_combo.set("(no FW available on any slave)")
            self._global_fw_selected_id = None

    def _on_global_fw_selected(self, event=None):
        """Set / clear global FW mode.

        - First entry ('(per-card selection)') → clear global mode; Flash All
          falls back to each card's own dropdown.
        - Any FW entry → enter global mode; Flash All ONLY targets slaves
          whose pool contains this FW (others SKIPPED, not silently flashed
          with a stale per-card selection).
        Also propagates the selection visually to matching cards' dropdowns
        so the user can see at a glance which slaves will fire.
        """
        idx = self.global_fw_combo.current()
        if idx < 0 or idx >= len(self._global_fw_ids):
            return
        fw_id = self._global_fw_ids[idx]
        self._global_fw_selected_id = fw_id

        if fw_id is None:
            self.app._log("NetFlash: per-card FW mode — Flash All uses each card's dropdown")
            return

        applied = 0
        skipped = 0
        for addr, c in self._cards.items():
            if not c["online"]:
                continue
            pool_ids = [f["id"] for f in c["fw_list"]]
            if fw_id in pool_ids:
                pool_idx = pool_ids.index(fw_id)
                c["fw_combo"].current(pool_idx)
                applied += 1
                self._card_log(addr, f"FW set: {fw_id}")
            else:
                skipped += 1

        self.app._log(
            f"NetFlash: global FW '{fw_id}' — will flash {applied} slave(s)"
            + (f", skip {skipped} (FW not in pool)" if skipped else "")
        )

    def _selected_fw_for(self, addr: int) -> Optional[str]:
        c = self._cards.get(addr)
        if not c or not c["fw_list"]:
            return None
        idx = c["fw_combo"].current()
        if idx < 0:
            return None
        return c["fw_list"][idx]["id"]

    def _flash_slave(self, addr: int):
        fw_id = self._selected_fw_for(addr)
        if not fw_id:
            messagebox.showwarning("Flash", "No firmware selected for this slave.")
            return
        c = self._cards[addr]
        self._card_log(addr, f"Flashing {fw_id}...")
        result = self.master_client.flash_slave(addr, fw_id)
        if not result.get("ok"):
            self._card_log(addr, f"✗ {result.get('error', 'flash rejected')}")
            return
        self._flash_in_progress = True

    def _flash_all(self):
        global_fw = self._global_fw_selected_id
        targets: dict[int, str] = {}
        skipped_no_match: list[int] = []   # online, but global FW not in pool

        for addr, c in self._cards.items():
            if not c["online"] or c["target_type"] == "no-target":
                continue
            if global_fw is not None:
                # Global mode: only flash slaves whose pool has this FW.
                pool_ids = [f["id"] for f in c["fw_list"]]
                if global_fw in pool_ids:
                    targets[addr] = global_fw
                else:
                    skipped_no_match.append(addr)
            else:
                fw_id = self._selected_fw_for(addr)
                if fw_id:
                    targets[addr] = fw_id

        if not targets:
            mode_hint = ("Selected global FW doesn't exist on any online slave."
                         if global_fw else
                         "Check online status + FW selection per card.")
            messagebox.showinfo("Flash All",
                                 f"No slaves ready to flash.\n\n{mode_hint}")
            return

        # Compose confirm dialog with target + skipped breakdown
        lines = [
            f"  ✓ {self._cards[a]['label_var'].get()} ← {fw}"
            for a, fw in targets.items()
        ]
        if skipped_no_match:
            lines.append("")
            lines.append(f"Skipped ({len(skipped_no_match)} — FW not in pool):")
            lines.extend(
                f"  ⊘ {self._cards[a]['label_var'].get()}"
                for a in skipped_no_match
            )

        mode_tag = f" (global FW: {global_fw})" if global_fw else " (per-card FW)"
        msg = f"Flash {len(targets)} slave(s){mode_tag}?\n\n" + "\n".join(lines)
        if not messagebox.askyesno("Confirm Flash All", msg):
            return

        self._session_ok = 0
        self._session_fail = 0
        self._flash_in_progress = True
        for addr, fw_id in targets.items():
            self._card_log(addr, f"Flashing {fw_id}...")
        result = self.master_client.flash_all(targets)
        rej = result.get("rejected", [])
        if rej:
            for a in rej:
                self._card_log(a, "✗ rejected")

    def _erase_all(self):
        online = [a for a, c in self._cards.items() if c["online"]]
        if not online:
            messagebox.showinfo("Erase All", "No online slaves to erase.")
            return
        if not messagebox.askyesno(
            "Confirm Erase All",
            f"Erase {len(online)} online slave(s)? This is irreversible."
        ):
            return
        for addr in online:
            self._card_log(addr, "Erasing...")
        self.master_client.erase_all()

    def _reboot_all(self):
        online = [a for a, c in self._cards.items() if c["online"]]
        if not online:
            messagebox.showinfo("Reboot All", "No online slaves to reboot.")
            return
        if not messagebox.askyesno(
            "Confirm Reboot All",
            f"Reboot {len(online)} online slave(s)?"
        ):
            return
        for addr in online:
            self._card_log(addr, "Rebooting...")
        self.master_client.reboot_all()

    def _reboot_slave(self, addr: int):
        self._card_log(addr, "Rebooting...")
        self.master_client.reboot_slave(addr)

    def _ping_slave(self, addr: int):
        # Re-fetch fresh slave info for this addr
        slaves = self.master_client.get_slaves()
        for slave in slaves:
            if slave.addr == addr:
                c = self._cards[addr]
                c["online"] = slave.online
                c["target_type"] = slave.target_type
                c["meta_var"].set(f"0x{slave.addr:02x} · {slave.target_type or 'no target'}")
                self._apply_card_visual(addr)
                self._card_log(addr, "Ping OK" if slave.online else "Offline")
                self._update_summary()
                return

    def _edit_label(self, addr: int):
        c = self._cards[addr]
        current = c["label_var"].get()
        new_label = simpledialog.askstring(
            "Edit Slave Label",
            f"Label for slave 0x{addr:02x}:",
            initialvalue=current, parent=self
        )
        if new_label is None or new_label.strip() == "":
            return
        new_label = new_label.strip()[:32]
        c["label_var"].set(new_label)

        # Persist
        labels = self.app.settings.setdefault("slave_labels", {})
        labels[f"0x{addr:02x}"] = new_label
        self._persist_settings()

    def _show_flash_all_toast(self):
        if self._session_ok == 0 and self._session_fail == 0:
            return
        title = "Flash Done" if self._session_fail == 0 else "Flash Done (with errors)"
        body = f"OK: {self._session_ok}  ·  Failed: {self._session_fail}"
        messagebox.showinfo(title, body)

    # ─────────────────────────── Settings persist ───────────────────────────

    def _persist_settings(self):
        import json
        import os
        try:
            with open(self.app._config_file, "w") as f:
                json.dump(self.app.settings, f, indent=2)
        except Exception as e:
            self.app._log(f"NetFlash: settings save failed — {e}")
