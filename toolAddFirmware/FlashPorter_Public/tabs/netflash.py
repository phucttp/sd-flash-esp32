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

from modules.master_client import MasterClient, SlaveInfo, SlaveStatus
from modules.net_flash import discover_nodes
from modules.theme import Colors, Fonts


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
        self.rowconfigure(2, weight=1)

        self._build_master_bar()
        self._build_hero_actions()
        self._build_cards_area()

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

    def _build_hero_actions(self):
        hero = tk.Frame(self, bg=Colors.BG_CARD, padx=14, pady=12)
        hero.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        hero.columnconfigure(3, weight=1)

        self.btn_flash_all = tk.Button(
            hero, text="⚡ FLASH ALL",
            font=(Fonts.FAMILY, 14, "bold"),
            bg=Colors.PRIMARY, fg=Colors.TEXT,
            activebackground=Colors.PRIMARY_DARK if hasattr(Colors, "PRIMARY_DARK") else Colors.PRIMARY,
            activeforeground=Colors.TEXT,
            relief="flat", borderwidth=0,
            padx=24, pady=10,
            cursor="hand2",
            command=self._flash_all,
            state="disabled",
        )
        self.btn_flash_all.grid(row=0, column=0, padx=(0, 10))

        self.btn_erase_all = tk.Button(
            hero, text="✖ Erase All",
            font=Fonts.bold(),
            bg=Colors.ERROR, fg=Colors.TEXT,
            relief="flat", borderwidth=0,
            padx=14, pady=10,
            cursor="hand2",
            command=self._erase_all,
            state="disabled",
        )
        self.btn_erase_all.grid(row=0, column=1, padx=(0, 8))

        self.btn_reboot_all = tk.Button(
            hero, text="↻ Reboot All",
            font=Fonts.bold(),
            bg=Colors.BG_DARK, fg=Colors.TEXT,
            relief="flat", borderwidth=1,
            padx=14, pady=10,
            cursor="hand2",
            command=self._reboot_all,
            state="disabled",
        )
        self.btn_reboot_all.grid(row=0, column=2, padx=(0, 14))

        self.summary_var = tk.StringVar(value="No master connected")
        tk.Label(hero, textvariable=self.summary_var,
                 bg=Colors.BG_CARD, fg=Colors.TEXT,
                 font=Fonts.bold()).grid(row=0, column=3, sticky="e")

    def _build_cards_area(self):
        container = ttk.Frame(self)
        container.grid(row=2, column=0, sticky="nsew")
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
        self.btn_flash_all.config(state="normal")
        self.btn_erase_all.config(state="normal")
        self.btn_reboot_all.config(state="normal")

        # Render slaves
        slaves = self.master_client.get_slaves()
        self._render_cards(slaves)
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

        # Row 3: FW dropdown
        fw_list = self.master_client.get_slave_fw_list(slave.addr)
        fw_values = [f["display"] for f in fw_list]
        fw_combo = ttk.Combobox(ct, values=fw_values, state="readonly",
                                font=Fonts.mono_small())
        fw_combo.grid(row=3, column=0, columnspan=3, sticky="ew", pady=(6, 4))
        if fw_values:
            fw_combo.current(0)
        else:
            fw_combo.set("(no FW available)")
            fw_combo.config(state="disabled")

        # Row 4: progress
        progress = ttk.Progressbar(ct, mode="determinate", maximum=100, length=200)
        progress.grid(row=4, column=0, columnspan=3, sticky="ew", pady=4)

        # Row 5: action buttons
        actions = tk.Frame(ct, bg=Colors.BG_CARD)
        actions.grid(row=5, column=0, columnspan=3, sticky="ew", pady=(4, 6))
        flash_btn = ttk.Button(actions, text="⚡ Flash", style="Primary.TButton",
                               command=lambda a=slave.addr: self._flash_slave(a))
        flash_btn.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4))
        reboot_btn = ttk.Button(actions, text="↻ Reboot", style="Secondary.TButton",
                                command=lambda a=slave.addr: self._reboot_slave(a))
        reboot_btn.pack(side=tk.LEFT, fill=tk.X, expand=True)

        # Row 6: mini log
        mini_log = tk.Text(ct, height=MINI_LOG_LINES,
                           bg=Colors.BG_DARK, fg=Colors.TEXT_MUTED,
                           font=Fonts.mono_small(),
                           wrap="word", relief="flat", borderwidth=0,
                           state="disabled")
        mini_log.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(2, 0))

        self._cards[slave.addr] = {
            "card_frame": cf,
            "dot": dot,
            "label_var": label_var,
            "meta_var": meta_var,
            "status_var": status_var,
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
        elif c["last_result"] == "fail":
            cf.configure(highlightbackground=Colors.ERROR, highlightthickness=2)
            c["dot"].configure(fg=Colors.ERROR)
            c["flash_btn"].config(state="normal")
            c["reboot_btn"].config(state="normal")
            if c["fw_list"]:
                c["fw_combo"].config(state="readonly")
        else:
            cf.configure(highlightbackground=Colors.SUCCESS, highlightthickness=2)
            c["dot"].configure(fg=Colors.SUCCESS)
            c["flash_btn"].config(state="normal")
            c["reboot_btn"].config(state="normal")
            if c["fw_list"]:
                c["fw_combo"].config(state="readonly")
            elif c["target_type"] == "no-target":
                c["flash_btn"].config(state="disabled")

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

        any_busy = False
        for st in statuses:
            if st.addr in self._cards:
                self._apply_status(st)
                if st.busy:
                    any_busy = True

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
        interval = POLL_INTERVAL_BUSY_MS if any_busy else POLL_INTERVAL_IDLE_MS
        self._poll_id = self.after(interval, self._tick_poll)

    def _apply_status(self, st: SlaveStatus):
        c = self._cards[st.addr]
        c["progress"]["value"] = st.progress
        c["status_var"].set(st.status_text)

    def _update_summary(self):
        total = len(self._cards)
        online = sum(1 for c in self._cards.values() if c["online"])
        parts = [f"{online}/{total} online"]
        if self._session_ok or self._session_fail:
            parts.append(f"{self._session_ok} OK")
            parts.append(f"{self._session_fail} ✗")
        self.summary_var.set("  ·  ".join(parts))

    # ─────────────────────────── Actions ───────────────────────────

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
        targets = {}
        for addr, c in self._cards.items():
            if not c["online"] or c["target_type"] == "no-target":
                continue
            fw_id = self._selected_fw_for(addr)
            if fw_id:
                targets[addr] = fw_id

        if not targets:
            messagebox.showinfo("Flash All",
                                 "No slaves ready to flash.\n"
                                 "(check online status + FW selection)")
            return

        msg = f"Flash {len(targets)} slave(s)?\n\n" + "\n".join(
            f"  • {self._cards[a]['label_var'].get()} ← {fw}"
            for a, fw in targets.items()
        )
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
