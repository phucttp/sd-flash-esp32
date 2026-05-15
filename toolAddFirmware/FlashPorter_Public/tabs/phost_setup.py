"""
phost_setup.py
==============
Phost Setup tab — provision a fresh Phost slave board before adding it
to the master's I2C bus.

Layout: 3 numbered step cards (Connect → Identity → Firmware) + progress
panel + big SETUP SLAVE button at the bottom.

Mock-backend friendly: the actual work is delegated to a PhostProvisioner
instance (real esptool flash + UART NVS command, or fake simulation).
"""

from __future__ import annotations

import os
import threading
import tkinter as tk
from collections import deque
from tkinter import filedialog, messagebox, ttk
from typing import Optional

from modules.phost_provisioner import (
    PhostProvisioner, SerialPort, ProvisionResult,
)
from modules.theme import Colors, Fonts


I2C_ADDR_RANGE = range(0x10, 0x20)   # 16 user-addressable slave slots
DEFAULT_BOARDS = ["ESP32-C3", "ESP32-S3", "ESP32-WROOM"]


class PhostSetupTab(ttk.Frame):
    """Wizard tab for one-time Phost slave provisioning."""

    def __init__(self, parent, app, provisioner: PhostProvisioner):
        super().__init__(parent, padding=14)
        self.app = app
        self.provisioner = provisioner

        # Step state
        self.com_port_var = tk.StringVar()
        self.board_var = tk.StringVar(value="ESP32-C3")
        self.i2c_addr_var = tk.StringVar()
        self.label_var = tk.StringVar()
        self.fw_source_var = tk.StringVar(value="builtin")
        self.fw_custom_path_var = tk.StringVar()

        # Runtime
        self._ports: list[SerialPort] = []
        self._test_ok = False
        self._busy = False
        self._log_lines: deque[str] = deque(maxlen=200)

        self.pack(fill=tk.BOTH, expand=True)
        self._build()

        # Populate ports + suggested addr/label after first paint
        self.after(150, self._refresh_ports)
        self.after(160, self._populate_suggested_identity)

    # ─────────────────────────── Layout ───────────────────────────

    def _build(self):
        # Intro card at the top
        self._build_intro_card(self)

        # 3 step cards stacked vertically
        self._build_step_connect(self)
        self._build_step_identity(self)
        self._build_step_firmware(self)

        # Progress + log panel
        self._build_progress(self)

        # Action button
        self._build_action_button(self)

    def _build_intro_card(self, parent):
        intro = tk.Frame(parent, bg=Colors.BG_CARD, padx=14, pady=10)
        intro.pack(fill=tk.X, pady=(0, 10))

        tk.Label(intro, text="Provision a new Phost slave",
                 bg=Colors.BG_CARD, fg=Colors.PRIMARY,
                 font=(Fonts.FAMILY, 13, "bold"),
                 anchor="w").pack(anchor="w")
        tk.Label(
            intro,
            text=("• Plug a fresh slave board into this PC via USB-TTL.\n"
                  "• Pick its COM port, assign an I2C address + label.\n"
                  "• After Setup: unplug, plug into master's I2C bus, "
                  "and it'll appear in the NetFlash tab."),
            bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
            font=Fonts.normal(), justify="left",
            anchor="w",
        ).pack(anchor="w", pady=(4, 0))

    def _step_frame(self, parent, label_text):
        grp = ttk.LabelFrame(parent, text=label_text, padding=10)
        grp.pack(fill=tk.X, pady=(0, 8))
        grp.columnconfigure(1, weight=1)
        return grp

    def _build_step_connect(self, parent):
        grp = self._step_frame(parent, "STEP 1 · Connection")

        ttk.Label(grp, text="COM Port:").grid(row=0, column=0, sticky="w", pady=4)
        self.port_combo = ttk.Combobox(grp, textvariable=self.com_port_var,
                                        state="readonly",
                                        font=Fonts.mono_small())
        self.port_combo.grid(row=0, column=1, sticky="ew", padx=8)
        self.port_combo.bind("<<ComboboxSelected>>", lambda e: self._set_test_ok(False))
        ttk.Button(grp, text="⟳ Refresh", style="Secondary.TButton",
                   command=self._refresh_ports).grid(row=0, column=2, padx=(0, 4))

        ttk.Label(grp, text="Slave Board:").grid(row=1, column=0, sticky="w", pady=4)
        self.board_combo = ttk.Combobox(grp, textvariable=self.board_var,
                                         values=DEFAULT_BOARDS, state="readonly")
        self.board_combo.grid(row=1, column=1, sticky="ew", padx=8, pady=4)

        # Test connection row
        test_row = ttk.Frame(grp)
        test_row.grid(row=2, column=1, columnspan=2, sticky="w", padx=8, pady=(6, 0))
        ttk.Button(test_row, text="▶ Test connection",
                   style="Primary.TButton",
                   command=self._test_connection).pack(side=tk.LEFT, padx=(0, 8))
        self.test_status_var = tk.StringVar(value="◯ Not tested")
        self.test_status_lbl = tk.Label(test_row, textvariable=self.test_status_var,
                                         fg=Colors.TEXT_MUTED, bg=Colors.BG_DARK,
                                         font=Fonts.bold())
        self.test_status_lbl.pack(side=tk.LEFT)

    def _build_step_identity(self, parent):
        grp = self._step_frame(parent, "STEP 2 · Identity")

        ttk.Label(grp, text="I2C Address:").grid(row=0, column=0, sticky="w", pady=4)
        self.addr_combo = ttk.Combobox(grp, textvariable=self.i2c_addr_var,
                                        font=Fonts.mono_small())
        self.addr_combo.grid(row=0, column=1, sticky="ew", padx=8, pady=4)

        self.existing_addrs_var = tk.StringVar(value="ℹ Existing on master: —")
        tk.Label(grp, textvariable=self.existing_addrs_var,
                 fg=Colors.TEXT_MUTED, bg=Colors.BG_DARK,
                 font=Fonts.small()).grid(row=1, column=1, sticky="w", padx=8)

        ttk.Label(grp, text="Label:").grid(row=2, column=0, sticky="w", pady=4)
        ttk.Entry(grp, textvariable=self.label_var).grid(
            row=2, column=1, sticky="ew", padx=8, pady=4)
        tk.Label(grp,
                 text="(saved to NetFlash slave_labels on success)",
                 fg=Colors.TEXT_MUTED, bg=Colors.BG_DARK,
                 font=Fonts.small()).grid(row=3, column=1, sticky="w", padx=8)

    def _build_step_firmware(self, parent):
        grp = self._step_frame(parent, "STEP 3 · Phost Firmware")

        ttk.Radiobutton(
            grp, text="Built-in: phost_main_v1.0  (recommended)",
            variable=self.fw_source_var, value="builtin",
            command=self._on_fw_source_changed,
        ).grid(row=0, column=0, columnspan=3, sticky="w", pady=2)

        ttk.Radiobutton(
            grp, text="Custom .bin file:",
            variable=self.fw_source_var, value="custom",
            command=self._on_fw_source_changed,
        ).grid(row=1, column=0, sticky="w", pady=2)

        self.fw_custom_entry = ttk.Entry(grp, textvariable=self.fw_custom_path_var,
                                          state="readonly", font=Fonts.mono_small())
        self.fw_custom_entry.grid(row=1, column=1, sticky="ew", padx=8)
        self.fw_browse_btn = ttk.Button(grp, text="Browse...",
                                         style="Secondary.TButton",
                                         command=self._browse_fw,
                                         state="disabled")
        self.fw_browse_btn.grid(row=1, column=2)

    def _build_progress(self, parent):
        grp = ttk.LabelFrame(parent, text="Progress", padding=10)
        grp.pack(fill=tk.BOTH, expand=True, pady=(2, 8))
        grp.columnconfigure(0, weight=1)
        grp.rowconfigure(2, weight=1)

        self.status_var = tk.StringVar(value="Ready")
        tk.Label(grp, textvariable=self.status_var,
                 fg=Colors.TEXT, bg=Colors.BG_DARK,
                 font=Fonts.bold(), anchor="w").grid(
            row=0, column=0, sticky="ew")

        self.progress = ttk.Progressbar(grp, mode="determinate", maximum=100)
        self.progress.grid(row=1, column=0, sticky="ew", pady=(4, 6))

        self.log_widget = tk.Text(
            grp, height=6, bg=Colors.BG_DARK, fg=Colors.TEXT_MUTED,
            font=Fonts.mono_small(), relief="flat", borderwidth=0,
            state="disabled", wrap="word",
        )
        self.log_widget.grid(row=2, column=0, sticky="nsew")

    def _build_action_button(self, parent):
        row = ttk.Frame(parent)
        row.pack(fill=tk.X)
        self.btn_setup = tk.Button(
            row, text="⚙  SETUP SLAVE",
            font=(Fonts.FAMILY, 14, "bold"),
            bg=Colors.PRIMARY, fg=Colors.TEXT,
            relief="flat", borderwidth=0,
            pady=14, cursor="hand2",
            command=self._setup_slave,
            state="disabled",
        )
        self.btn_setup.pack(fill=tk.X)

    # ─────────────────────────── Step 1 helpers ───────────────────────────

    def _refresh_ports(self):
        self._ports = self.provisioner.list_ports()
        values = [f"{p.device}  —  {p.description.replace(p.device + ' ', '').strip('()')}"
                  for p in self._ports]
        self.port_combo["values"] = values
        if values and not self.com_port_var.get():
            self.port_combo.current(0)
        self._set_test_ok(False)

    def _selected_port_device(self) -> Optional[str]:
        s = self.com_port_var.get()
        if not s:
            return None
        # Recover device from the formatted label
        return s.split("  —  ")[0].strip()

    def _test_connection(self):
        port = self._selected_port_device()
        if not port:
            messagebox.showwarning("Test", "Pick a COM port first.")
            return
        self.test_status_var.set("◯ Testing...")
        self.test_status_lbl.configure(fg=Colors.TEXT_MUTED)
        threading.Thread(
            target=self._do_test, args=(port,), daemon=True,
        ).start()

    def _do_test(self, port: str):
        result = self.provisioner.test_connection(port)
        self.after(0, self._on_test_done, result)

    def _on_test_done(self, result: dict):
        if result.get("ok"):
            chip = result.get("chip_info", "slave")
            self.test_status_var.set(f"● Detected · {chip}")
            self.test_status_lbl.configure(fg=Colors.SUCCESS)
            self._log(f"Test OK — {chip}")
            self._set_test_ok(True)
        else:
            self.test_status_var.set(f"✗ {result.get('error', 'unknown')}")
            self.test_status_lbl.configure(fg=Colors.ERROR)
            self._log(f"Test FAILED — {result.get('error')}")
            self._set_test_ok(False)

    def _set_test_ok(self, ok: bool):
        self._test_ok = ok
        self._update_setup_button_state()

    # ─────────────────────────── Step 2 helpers ───────────────────────────

    def _existing_master_addrs(self) -> list[int]:
        """Query master_client for currently-known slave addrs (if connected)."""
        mc = getattr(self.app, "master_client", None)
        if mc is None or not getattr(mc, "is_connected", lambda: False)():
            return []
        try:
            return [s.addr for s in mc.get_slaves()]
        except Exception:
            return []

    def _populate_suggested_identity(self):
        existing = self._existing_master_addrs()
        existing_set = set(existing)
        free = [a for a in I2C_ADDR_RANGE if a not in existing_set]

        if existing:
            self.existing_addrs_var.set(
                "ℹ Existing on master: "
                + ", ".join(f"0x{a:02x}" for a in sorted(existing))
            )
        else:
            self.existing_addrs_var.set(
                "ℹ Master not connected — pick any address 0x10..0x1F"
            )

        # Combobox values: all 16 addrs, mark in-use ones with a tag
        values = [
            f"0x{a:02x}" + ("  (in use)" if a in existing_set else "")
            for a in I2C_ADDR_RANGE
        ]
        self.addr_combo["values"] = values
        # Pick first free
        next_free = free[0] if free else I2C_ADDR_RANGE.start
        self.addr_combo.set(f"0x{next_free:02x}")

        # Suggested label
        self.label_var.set(f"Phost-{len(existing) + 1}")

    def _parse_selected_addr(self) -> Optional[int]:
        s = self.i2c_addr_var.get().strip().split()[0]  # 0x14 or 0x14
        if not s:
            return None
        try:
            return int(s, 16)
        except ValueError:
            return None

    # ─────────────────────────── Step 3 helpers ───────────────────────────

    def _on_fw_source_changed(self):
        custom = self.fw_source_var.get() == "custom"
        self.fw_browse_btn.config(state="normal" if custom else "disabled")
        self.fw_custom_entry.config(state="normal" if custom else "readonly")

    def _browse_fw(self):
        path = filedialog.askopenfilename(
            title="Select phost firmware .bin",
            filetypes=[("Binary firmware", "*.bin"), ("All files", "*.*")],
        )
        if path:
            self.fw_custom_path_var.set(path)

    def _resolve_fw_bytes(self) -> Optional[bytes]:
        if self.fw_source_var.get() == "builtin":
            # Try to read packaged resource; if missing, return empty bytes so
            # the mock provisioner can still simulate (real impl must enforce).
            res_path = os.path.join(
                os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                "resources", "phost_main.bin",
            )
            if os.path.exists(res_path):
                with open(res_path, "rb") as f:
                    return f.read()
            return b""  # mock-friendly placeholder
        path = self.fw_custom_path_var.get()
        if not path or not os.path.exists(path):
            return None
        with open(path, "rb") as f:
            return f.read()

    # ─────────────────────────── Action ───────────────────────────

    def _update_setup_button_state(self):
        ready = (self._test_ok and not self._busy
                 and self._parse_selected_addr() is not None
                 and self.label_var.get().strip())
        self.btn_setup.config(state="normal" if ready else "disabled")

    def _setup_slave(self):
        port = self._selected_port_device()
        addr = self._parse_selected_addr()
        label = self.label_var.get().strip()
        fw_bytes = self._resolve_fw_bytes()

        if not port:
            return messagebox.showerror("Setup", "Pick a COM port.")
        if addr is None:
            return messagebox.showerror("Setup", "Pick a valid I2C address.")
        if not label:
            return messagebox.showerror("Setup", "Label cannot be empty.")
        if fw_bytes is None:
            return messagebox.showerror(
                "Setup", "Custom firmware file not found.")

        # Confirm overwrite of existing addr
        if addr in self._existing_master_addrs():
            if not messagebox.askyesno(
                "Confirm",
                f"Address 0x{addr:02x} is already used by another slave on the "
                "master. Setup will configure THIS slave with the same address, "
                "which will cause an I2C conflict.\n\nProceed anyway?"
            ):
                return

        if not messagebox.askyesno(
            "Confirm Setup",
            f"Provision slave on {port} as '{label}' @ 0x{addr:02x}?\n\n"
            "This will erase the slave and flash the phost firmware."
        ):
            return

        self._busy = True
        self.btn_setup.config(state="disabled", text="Provisioning...")
        self.progress["value"] = 0
        self.status_var.set("Starting...")
        self._log("─" * 50)
        self._log(f"Provisioning {label} on {port} @ 0x{addr:02x}")

        threading.Thread(
            target=self._do_provision,
            args=(port, fw_bytes, addr, label),
            daemon=True,
        ).start()

    def _do_provision(self, port: str, fw_bytes: bytes, addr: int, label: str):
        def progress_cb(pct: int, status: str):
            self.after(0, self._set_progress, pct, status)

        def log_cb(line: str):
            self.after(0, self._log, line)

        result = self.provisioner.provision_slave(
            port, fw_bytes, addr, progress_cb, log_cb,
        )
        self.after(0, self._on_provision_done, result, addr, label)

    def _on_provision_done(self, result: ProvisionResult, addr: int, label: str):
        self._busy = False
        self.btn_setup.config(text="⚙  SETUP SLAVE")
        if result.ok:
            self.status_var.set(
                f"✓ Done in {result.duration_s:.1f}s — "
                f"unplug and move to I2C bus"
            )
            self._log(f"✓ Provisioned in {result.duration_s:.1f}s")
            # Persist label to NetFlash slave_labels so it shows correctly
            # when the slave appears on master.
            labels = self.app.settings.setdefault("slave_labels", {})
            labels[f"0x{addr:02x}"] = label
            self._persist_settings()
            messagebox.showinfo(
                "Setup Complete",
                f"Slave '{label}' provisioned on 0x{addr:02x}.\n\n"
                "Unplug USB-TTL and plug into master's I2C bus.\n"
                "Then go to NetFlash tab and click 🔍 Find or wait for "
                "next status poll."
            )
            self._populate_suggested_identity()  # bump to next free addr
        else:
            self.status_var.set(f"✗ {result.error}")
            self._log(f"✗ Failed: {result.error}")
            messagebox.showerror("Setup Failed",
                                  f"{result.error}\n\nSee progress log.")
        self._update_setup_button_state()

    # ─────────────────────────── Misc helpers ───────────────────────────

    def _set_progress(self, pct: int, status: str):
        self.progress["value"] = pct
        self.status_var.set(f"[{pct}%] {status}")

    def _log(self, line: str):
        self._log_lines.append(line)
        self.log_widget.config(state="normal")
        self.log_widget.delete("1.0", tk.END)
        self.log_widget.insert(tk.END, "\n".join(self._log_lines))
        self.log_widget.see(tk.END)
        self.log_widget.config(state="disabled")

    def _persist_settings(self):
        import json
        try:
            with open(self.app._config_file, "w") as f:
                json.dump(self.app.settings, f, indent=2)
        except Exception as e:
            self.app._log(f"PhostSetup: settings save failed — {e}")
