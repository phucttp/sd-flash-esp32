"""
settings_tab.py
===============
Settings tab: library path, encryption keys, git URL, account.

This tab is extracted from main.pyw as the pattern-validating tab for the
multi-file split. State variables (lib_path_var, enc_key, enc_iv, etc.) live
on MainApp because other tabs (Library/SD ops) also read them.
"""

import json
import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from modules.theme import Colors, Fonts


class SettingsTab(ttk.Frame):
    """Settings tab — paths, encryption, git, account."""

    def __init__(self, parent, app, config_file: str):
        super().__init__(parent, padding=20)
        self.app = app
        self.config_file = config_file
        self.pack(fill=tk.BOTH, expand=True)
        self._build()

    def _build(self):
        app = self.app

        # Library Path
        grp_lib = ttk.LabelFrame(self, text="Firmware Library Location", padding=15)
        grp_lib.pack(fill=tk.X, pady=(0, 15))
        grp_lib.columnconfigure(1, weight=1)

        ttk.Label(grp_lib, text="Library Path:").grid(row=0, column=0, sticky="w", pady=8)
        ttk.Entry(grp_lib, textvariable=app.lib_path_var,
                  font=Fonts.mono_small()).grid(row=0, column=1, padx=10, sticky="ew")
        ttk.Button(grp_lib, text="...", width=3, style="Icon.TButton",
                   command=self._choose_lib_path).grid(row=0, column=2)

        ttk.Label(grp_lib, text="(Restart app after changing)",
                  style="Muted.TLabel").grid(row=1, column=1, sticky="w", padx=10)

        # Encryption
        grp_enc = ttk.LabelFrame(self, text="Encryption Settings (AES-128-CBC)", padding=15)
        grp_enc.pack(fill=tk.X, pady=(0, 15))
        grp_enc.columnconfigure(1, weight=1)

        ttk.Label(grp_enc, text="AES Key (16 chars):").grid(row=0, column=0, sticky="w", pady=8)
        ttk.Entry(grp_enc, textvariable=app.enc_key,
                  font=Fonts.mono()).grid(row=0, column=1, padx=10, sticky="ew")

        ttk.Label(grp_enc, text="AES IV (16 chars):").grid(row=1, column=0, sticky="w", pady=8)
        ttk.Entry(grp_enc, textvariable=app.enc_iv,
                  font=Fonts.mono()).grid(row=1, column=1, padx=10, sticky="ew")

        # Git
        grp_git = ttk.LabelFrame(self, text="Git Repository", padding=15)
        grp_git.pack(fill=tk.X, pady=(0, 15))
        grp_git.columnconfigure(1, weight=1)

        ttk.Label(grp_git, text="Repository URL:").grid(row=0, column=0, sticky="w", pady=8)
        ttk.Entry(grp_git, textvariable=app.git_url_var,
                  font=Fonts.mono()).grid(row=0, column=1, padx=10, sticky="ew")

        # Account
        grp_acc = ttk.LabelFrame(self, text="Account", padding=15)
        grp_acc.pack(fill=tk.X, pady=(0, 15))

        ttk.Label(grp_acc, text=f"Logged in as: {app.auth.get_username()}").pack(anchor="w")
        ttk.Button(grp_acc, text="Change Password", style="Secondary.TButton",
                   command=self._change_password).pack(anchor="w", pady=5)

        # Save
        ttk.Button(self, text="SAVE SETTINGS", style="Primary.TButton",
                   command=self._save_settings).pack(pady=20)

    # ---------- helpers ----------

    def _choose_lib_path(self):
        path = filedialog.askdirectory(title="Select Firmware Library Folder")
        if path:
            self.app.lib_path_var.set(path)

    def _save_settings(self):
        """Persist user-facing settings.

        NOTE: This merges into existing settings dict (preserving keys set by
        other subsystems like NetFlash `net_nodes` and OLED `oled_config`).
        Previous behavior wiped those keys — this fixes a long-standing bug.
        """
        app = self.app
        app.settings.update({
            "key": app.enc_key.get(),
            "iv": app.enc_iv.get(),
            "sd_path": app.sd_path_var.get(),
            "git_url": app.git_url_var.get(),
            "lib_path": app.lib_path_var.get(),
        })
        try:
            with open(self.config_file, "w") as f:
                json.dump(app.settings, f, indent=2)
            app._log("Settings saved.")
            messagebox.showinfo("Saved", "Settings saved successfully.")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save: {e}")

    def _change_password(self):
        app = self.app
        dialog = tk.Toplevel(app)
        dialog.title("Change Password")
        dialog.geometry("350x250")
        dialog.configure(bg=Colors.BG_DARK)
        dialog.transient(app)
        dialog.grab_set()

        dialog.update_idletasks()
        x = (dialog.winfo_screenwidth() - 350) // 2
        y = (dialog.winfo_screenheight() - 250) // 2
        dialog.geometry(f"+{x}+{y}")

        frm = ttk.Frame(dialog, padding=25)
        frm.pack(fill=tk.BOTH, expand=True)

        tk.Label(frm, text="Change Password", font=Fonts.heading(),
                 fg=Colors.PRIMARY, bg=Colors.BG_DARK).pack(pady=(0, 20))

        ttk.Label(frm, text="Current Password:").pack(anchor="w")
        current_var = tk.StringVar()
        ttk.Entry(frm, textvariable=current_var, show="*",
                  font=Fonts.normal()).pack(fill=tk.X, pady=(0, 12))

        ttk.Label(frm, text="New Password:").pack(anchor="w")
        new_var = tk.StringVar()
        ttk.Entry(frm, textvariable=new_var, show="*",
                  font=Fonts.normal()).pack(fill=tk.X, pady=(0, 12))

        status = ttk.Label(frm, text="", foreground=Colors.ERROR)
        status.pack(pady=5)

        def submit():
            success, msg = app.auth.change_password(current_var.get(), new_var.get())
            if success:
                messagebox.showinfo("Success", msg)
                dialog.destroy()
            else:
                status.config(text=msg)

        ttk.Button(frm, text="Change Password", style="Primary.TButton",
                   command=submit).pack(pady=10, fill=tk.X)
