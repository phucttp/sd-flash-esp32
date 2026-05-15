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
from tkinter import ttk, messagebox, scrolledtext
from typing import Optional

# Add modules to path
if getattr(sys, 'frozen', False):
    APP_DIR = os.path.dirname(sys.executable)
else:
    APP_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, APP_DIR)

# Import modules
from modules.auth import AuthManager
from modules.firmware_lib import FirmwareLibrary
from modules.sd_card import SDCardManager
from modules.git_sync import GitManager
from modules.theme import Colors, Fonts, apply_theme, GradientFrame, StatusBadge
# net_flash module is now used only by tabs/netflash.py (NetFlashTab)
from modules.master_client import make_master_client

# Tab classes (Phase 1+: split from monolith main.pyw)
from tabs.settings_tab import SettingsTab
from tabs.netflash import NetFlashTab
from tabs.firmware_manager import FirmwareManagerTab

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
        # sd_path_var now owned by FirmwareManagerTab (Phase 3); kept here only
        # because settings_tab.py and external persist code may reference it.
        self.sd_path_var = tk.StringVar(value=self.settings.get("sd_path", ""))
        self.git_url_var = tk.StringVar(value=self.settings.get("git_url", ""))
        self.lib_path_var = tk.StringVar(value=self.settings.get("lib_path", self.lib.lib_root))

        # NetFlash state is owned by NetFlashTab (Phase 2 redesign).
        # Firmware Manager state (add_fw_id_var et al.) is owned by FirmwareManagerTab (Phase 3).

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
        # FirmwareManagerTab schedules its own _refresh_firmware_list via after()

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

        # Tab 1: Firmware Manager (merged Add + Library, Phase 3)
        frm_fw = ttk.Frame(nb)
        nb.add(frm_fw, text="  Firmware Manager  ")
        self.fw_manager_tab = FirmwareManagerTab(frm_fw, self)

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



# ==================== Entry Point ====================

if __name__ == "__main__":
    # Need simpledialog for reset
    import tkinter.simpledialog

    app = MainApp()
    app.mainloop()
