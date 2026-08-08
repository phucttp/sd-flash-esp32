"""
phost_setup.py
==============
Phost Setup tab — one-time provisioning of a fresh phost slave.

Flow (post-refactor):
  1. User physically plugs new phost into master's dedicated UART slot.
  2. PC sends ONE command to master: provision_slave(addr, label).
  3. Master uses its pre-flashed phost_main partition, flashes the slave
     via UART, writes I2C addr to slave's NVS, reports progress over HTTP.
  4. PC shows progress + log + result.

PC tool no longer needs esptool, USB-TTL drivers, or COM port handling —
all that work lives on the master firmware.
"""

from __future__ import annotations

import threading
import tkinter as tk
from collections import deque
from tkinter import messagebox, ttk
from typing import Optional

from modules.master_client import MasterClient, ProvisionResult
from modules.theme import Colors, Fonts


I2C_ADDR_RANGE = range(0x10, 0x20)   # 16 user-addressable slave slots


class PhostSetupTab(ttk.Frame):
    """Slim 1-screen wizard: pick addr + label, click Provision."""

    def __init__(self, parent, app, master_client: MasterClient):
        super().__init__(parent, padding=14)
        self.app = app
        self.master_client = master_client  # NEVER name this self.master (Tk reserved)

        # Form state
        self.i2c_addr_var = tk.StringVar()
        self.label_var = tk.StringVar()
        self.collision_var = tk.StringVar(value="")

        # Runtime
        self._busy = False
        self._log_lines: deque[str] = deque(maxlen=200)
        self._existing_addrs: list[int] = []

        self.pack(fill=tk.BOTH, expand=True)
        self._build()

        # Refresh existing addrs + suggest next free. Delayed past the
        # NetFlash mock auto-connect (~300ms + handshake) so the master is
        # connected by the time we query it.
        self.after(900, self.refresh_from_master)

    # ─────────────────────────── Layout ───────────────────────────

    def _build(self):
        self._build_intro_card(self)

        # Two columns: form left, progress right
        paned = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True)

        left = ttk.Frame(paned)
        right = ttk.Frame(paned)
        paned.add(left, weight=1)
        paned.add(right, weight=1)

        self._build_identity(left)
        self._build_provision(right)

    def _build_intro_card(self, parent):
        intro = tk.Frame(parent, bg=Colors.BG_CARD, padx=14, pady=10)
        intro.pack(fill=tk.X, pady=(0, 10))

        tk.Label(intro, text="Provision a new Phost slave",
                 bg=Colors.BG_CARD, fg=Colors.PRIMARY,
                 font=(Fonts.FAMILY, 13, "bold"),
                 anchor="w").pack(anchor="w")
        tk.Label(
            intro,
            text=("• Plug the new phost into the master's provisioning slot "
                  "(UART pins).\n"
                  "• Pick an I2C address + label, then click Provision.\n"
                  "• Master flashes phost_main + writes the I2C address; "
                  "the PC only watches progress.\n"
                  "• When done: unplug the phost from the slot and move it "
                  "to the main I2C bus."),
            bg=Colors.BG_CARD, fg=Colors.TEXT_MUTED,
            font=Fonts.normal(), justify="left",
            anchor="w",
        ).pack(anchor="w", pady=(4, 0))

    def _build_identity(self, parent):
        grp = ttk.LabelFrame(parent, text="Slave Identity", padding=10)
        grp.pack(fill=tk.X, pady=(0, 8))
        grp.columnconfigure(1, weight=1)

        ttk.Label(grp, text="I2C Address:").grid(row=0, column=0, sticky="w", pady=4)
        self.addr_combo = ttk.Combobox(grp, textvariable=self.i2c_addr_var,
                                        font=Fonts.mono_small())
        self.addr_combo.grid(row=0, column=1, sticky="ew", padx=8, pady=4)
        self.addr_combo.bind("<<ComboboxSelected>>",
                              lambda e: self._on_addr_changed())
        self.addr_combo.bind("<KeyRelease>",
                              lambda e: self._on_addr_changed())

        self.existing_addrs_var = tk.StringVar(value="ℹ Existing on master: —")
        self.existing_addrs_lbl = tk.Label(
            grp, textvariable=self.existing_addrs_var,
            fg=Colors.TEXT_MUTED, bg=Colors.BG_CARD,
            font=Fonts.small(), justify="left", anchor="w")
        self.existing_addrs_lbl.grid(row=1, column=1, sticky="ew", padx=8)
        # Wrap to the actual cell width — wraplength only changes height, so
        # binding it to <Configure> causes no resize feedback loop.
        self.existing_addrs_lbl.bind(
            "<Configure>",
            lambda e: self.existing_addrs_lbl.configure(
                wraplength=max(e.width - 4, 120)))

        self.collision_label = tk.Label(
            grp, textvariable=self.collision_var,
            fg=Colors.ERROR, bg=Colors.BG_CARD, font=Fonts.bold(),
        )
        self.collision_label.grid(row=2, column=1, sticky="w", padx=8, pady=(4, 0))

        ttk.Label(grp, text="Label:").grid(row=3, column=0, sticky="w", pady=4)
        ttk.Entry(grp, textvariable=self.label_var).grid(
            row=3, column=1, sticky="ew", padx=8, pady=4)
        self.label_var.trace_add("write",
                                  lambda *a: self._update_setup_button_state())

        # Refresh button to re-query master's slave list
        ttk.Button(grp, text="⟳ Refresh from master",
                   style="Secondary.TButton",
                   command=self.refresh_from_master,
                   ).grid(row=4, column=1, sticky="e", padx=8, pady=(6, 0))

    def _build_provision(self, parent):
        grp = ttk.LabelFrame(parent, text="Provision", padding=10)
        grp.pack(fill=tk.BOTH, expand=True)

        # Big action button pinned to bottom (always visible)
        self.btn_setup = tk.Button(
            grp, text="⚙  PROVISION SLAVE ON MASTER",
            font=(Fonts.FAMILY, 14, "bold"),
            bg=Colors.PRIMARY, fg=Colors.TEXT_DARK,
            activeforeground=Colors.TEXT_DARK,
            relief="flat", borderwidth=0,
            pady=12, cursor="hand2",
            command=self._provision,
            state="disabled",
        )
        self.btn_setup.pack(side=tk.BOTTOM, fill=tk.X, pady=(8, 0))

        self.status_var = tk.StringVar(value="Ready")
        tk.Label(grp, textvariable=self.status_var,
                 fg=Colors.TEXT, bg=Colors.BG_CARD,
                 font=Fonts.bold(), anchor="w").pack(fill=tk.X)

        self.progress = ttk.Progressbar(grp, mode="determinate", maximum=100)
        self.progress.pack(fill=tk.X, pady=(4, 6))

        self.log_widget = tk.Text(
            grp, height=10, bg=Colors.BG_INPUT, fg=Colors.TEXT,
            font=Fonts.mono_small(), relief="flat", borderwidth=0,
            state="disabled", wrap="word",
        )
        self.log_widget.pack(fill=tk.BOTH, expand=True)

    # ─────────────────────────── Data refresh ───────────────────────────

    def refresh_from_master(self):
        """Query master_client for existing slave addrs + suggest next free."""
        existing: list[int] = []
        labels: dict[int, str] = {}
        if self.master_client.is_connected():
            try:
                slaves = self.master_client.get_slaves()
                existing = [s.addr for s in slaves]
                labels = {s.addr: s.label for s in slaves}
            except Exception as e:
                self._log(f"refresh failed: {e}")
        self._existing_addrs = existing

        if existing:
            self.existing_addrs_var.set(
                "ℹ Existing on master: "
                + ", ".join(f"0x{a:02x} ({labels.get(a, '?')})"
                            for a in sorted(existing))
            )
        else:
            self.existing_addrs_var.set(
                "ℹ Master not connected (or no slaves) — pick any 0x10..0x1F"
            )

        # Combobox values: all 16 addrs, mark in-use ones with a tag
        existing_set = set(existing)
        values = [
            f"0x{a:02x}" + ("  (in use)" if a in existing_set else "")
            for a in I2C_ADDR_RANGE
        ]
        self.addr_combo["values"] = values

        # Auto-pick first free addr
        free = [a for a in I2C_ADDR_RANGE if a not in existing_set]
        next_free = free[0] if free else I2C_ADDR_RANGE.start
        self.addr_combo.set(f"0x{next_free:02x}")

        # Suggested label
        if not self.label_var.get():
            self.label_var.set(f"Phost-{len(existing) + 1}")

        self._on_addr_changed()

    def _parse_selected_addr(self) -> Optional[int]:
        s = self.i2c_addr_var.get().strip().split()[0] if self.i2c_addr_var.get() else ""
        if not s:
            return None
        try:
            return int(s, 16)
        except ValueError:
            return None

    def _on_addr_changed(self):
        addr = self._parse_selected_addr()
        if addr is None:
            self.collision_var.set("✗ Invalid address — use 0x10..0x1F")
        elif addr not in I2C_ADDR_RANGE:
            self.collision_var.set(
                f"✗ 0x{addr:02x} out of range (use 0x10..0x1F)"
            )
        elif addr in self._existing_addrs:
            self.collision_var.set(
                f"⚠ 0x{addr:02x} already in use — pick a free address"
            )
        else:
            self.collision_var.set("")
        self._update_setup_button_state()

    # ─────────────────────────── Provision flow ───────────────────────────

    def _update_setup_button_state(self):
        addr = self._parse_selected_addr()
        ready = (
            not self._busy
            and self.master_client.is_connected()
            and addr is not None
            and addr in I2C_ADDR_RANGE
            and addr not in self._existing_addrs
            and bool(self.label_var.get().strip())
        )
        self.btn_setup.config(state="normal" if ready else "disabled")

    def _provision(self):
        addr = self._parse_selected_addr()
        label = self.label_var.get().strip()
        if addr is None or not label:
            return  # button should be disabled, but defensive

        if not self.master_client.is_connected():
            return messagebox.showerror(
                "Provision",
                "Master not connected. Open NetFlash tab and Connect first.",
            )

        if not messagebox.askyesno(
            "Confirm Provision",
            f"Provision slave on master's UART slot as '{label}' @ 0x{addr:02x}?\n\n"
            "Master will erase the slave and flash phost_main from its own "
            "partition. Make sure the slave is plugged into the provisioning "
            "slot before continuing."
        ):
            return

        self._busy = True
        self.btn_setup.config(state="disabled", text="Provisioning...")
        self.progress["value"] = 0
        self.status_var.set("Starting...")
        self._log("─" * 50)
        self._log(f"Provisioning '{label}' @ 0x{addr:02x} via master")

        threading.Thread(
            target=self._do_provision, args=(addr, label),
            daemon=True,
        ).start()

    def _do_provision(self, addr: int, label: str):
        def progress_cb(pct: int, status: str):
            self.after(0, self._set_progress, pct, status)

        def log_cb(line: str):
            self.after(0, self._log, line)

        result = self.master_client.provision_slave(
            addr, label, progress_cb, log_cb,
        )
        self.after(0, self._on_done, result, addr, label)

    def _on_done(self, result: ProvisionResult, addr: int, label: str):
        self._busy = False
        self.btn_setup.config(text="⚙  PROVISION SLAVE ON MASTER")

        if result.ok:
            self.status_var.set(
                f"✓ Done in {result.duration_s:.1f}s — "
                "unplug + move to I2C bus"
            )
            self._log(f"✓ Provisioned in {result.duration_s:.1f}s")

            # Persist label so NetFlash tab shows it correctly
            labels = self.app.settings.setdefault("slave_labels", {})
            labels[f"0x{addr:02x}"] = label
            self._persist_settings()

            messagebox.showinfo(
                "Setup Complete",
                f"Slave '{label}' provisioned on 0x{addr:02x}.\n\n"
                "Unplug the phost from the provisioning slot and move it to "
                "the main I2C bus.\n"
                "The slave will appear in the NetFlash tab on the next poll."
            )
            # Refresh so the new slave shows up in 'existing' list,
            # button auto-picks next free
            self.refresh_from_master()
        else:
            self.status_var.set(f"✗ {result.error}")
            self._log(f"✗ Failed: {result.error}")
            messagebox.showerror("Provision Failed",
                                  f"{result.error}\n\nSee the progress log for details.")
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
