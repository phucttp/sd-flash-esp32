"""
git_sync.py - Git Synchronization Module
=========================================
Handles exporting firmware to Git repository (incremental, no force push).
Includes auto-install Git if not present.
"""

import os
import subprocess
import json
import shutil
from typing import List, Dict, Tuple, Any, Optional, Callable

from .utils import FileNames, calculate_md5_bytes, ensure_dir, get_app_dir
from .crypto import FWEncryptor, load_crypto


class GitManager:
    """Manages Git repository operations for firmware distribution."""

    # Git installer URLs (64-bit portable)
    GIT_INSTALLER_URL = "https://github.com/git-for-windows/git/releases/download/v2.43.0.windows.1/PortableGit-2.43.0-64-bit.7z.exe"

    def __init__(self, release_dir: Optional[str] = None):
        """
        Initialize Git manager.

        Args:
            release_dir: Directory for Git releases.
                        Default: <parent of app_dir>/_release_for_git (same as old FlashPorter)
        """
        if release_dir is None:
            # Use parent directory (toolAddFirmware) like original FlashPorter.pyw
            app_dir = get_app_dir()
            parent_dir = os.path.dirname(app_dir)
            release_dir = os.path.join(parent_dir, "_release_for_git")
        self.release_dir = release_dir
        self.repo_url = ""

    def is_git_installed(self) -> bool:
        """Check if Git is installed and available."""
        try:
            result = subprocess.run(
                ["git", "--version"],
                capture_output=True,
                text=True,
                timeout=5
            )
            return result.returncode == 0
        except (subprocess.TimeoutExpired, FileNotFoundError):
            return False

    def get_git_version(self) -> Optional[str]:
        """Get installed Git version."""
        try:
            result = subprocess.run(
                ["git", "--version"],
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                return result.stdout.strip()
        except:
            pass
        return None

    def install_git_prompt(self) -> Tuple[bool, str]:
        """
        Show instructions to install Git.
        On Windows, can auto-download portable version.

        Returns:
            Tuple of (user_wants_install, message)
        """
        if os.name == "nt":
            return True, (
                "Git is not installed.\n\n"
                "Options:\n"
                "1. Download from: https://git-scm.com/download/win\n"
                "2. Install with winget: winget install Git.Git\n"
                "3. Install with chocolatey: choco install git\n\n"
                "After installing, restart this application."
            )
        else:
            return True, (
                "Git is not installed.\n\n"
                "Install with:\n"
                "  Ubuntu/Debian: sudo apt install git\n"
                "  macOS: brew install git\n"
                "  Fedora: sudo dnf install git\n\n"
                "After installing, restart this application."
            )

    def create_git_install_script(self) -> str:
        """
        Create a batch script to install Git on Windows.

        Returns:
            Path to the created script
        """
        script_path = os.path.join(get_app_dir(), "install_git.bat")

        script_content = '''@echo off
title Installing Git for Windows
color 0A
echo ==========================================
echo      INSTALLING GIT FOR WINDOWS
echo ==========================================
echo.
echo Checking winget...
winget --version >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Found winget! Installing Git...
    winget install Git.Git --accept-package-agreements --accept-source-agreements
    goto :done
)

echo.
echo winget not found. Please install Git manually:
echo   1. Go to: https://git-scm.com/download/win
echo   2. Download and run the installer
echo   3. Restart FlashPorter after installation
echo.
start https://git-scm.com/download/win

:done
echo.
pause
'''

        with open(script_path, "w") as f:
            f.write(script_content)

        return script_path

    def set_repo_url(self, url: str):
        """Set remote repository URL."""
        self.repo_url = url.strip()

    def is_git_repo(self) -> bool:
        """Check if release directory is a Git repository."""
        git_dir = os.path.join(self.release_dir, ".git")
        return os.path.isdir(git_dir)

    def init_repo(self) -> Tuple[bool, str]:
        """
        Initialize Git repository in release directory.

        Returns:
            Tuple of (success, message)
        """
        ensure_dir(self.release_dir)

        if self.is_git_repo():
            return True, "Repository already initialized"

        try:
            # git init
            result = subprocess.run(
                ["git", "init"],
                cwd=self.release_dir,
                capture_output=True,
                text=True,
                timeout=30
            )
            if result.returncode != 0:
                return False, f"git init failed: {result.stderr}"

            # git branch -M main
            subprocess.run(
                ["git", "branch", "-M", "main"],
                cwd=self.release_dir,
                capture_output=True,
                timeout=10
            )

            return True, "Repository initialized"

        except Exception as e:
            return False, f"Failed to init repo: {e}"

    def export_firmware(
        self,
        fw_id: str,
        src_dir: str,
        local_meta: Dict[str, Any],
        encryptor: FWEncryptor,
        progress_callback: Optional[Callable[[str], None]] = None
    ) -> Tuple[bool, str]:
        """
        Export firmware to release directory (encrypted for Git).

        Args:
            fw_id: Firmware ID
            src_dir: Source directory containing firmware files
            local_meta: Local metadata for the firmware
            encryptor: FWEncryptor instance
            progress_callback: Optional callback for progress messages

        Returns:
            Tuple of (success, message)
        """
        is_stm32 = "STM32" in local_meta.get("device_type", "").upper()

        if not is_stm32:
            if not load_crypto():
                return False, "Crypto library not available"

        ensure_dir(self.release_dir)

        # Create firmware output directory
        fw_out_dir = os.path.join(self.release_dir, fw_id)
        if os.path.exists(fw_out_dir):
            shutil.rmtree(fw_out_dir)
        os.makedirs(fw_out_dir, exist_ok=True)

        # Create export metadata
        export_meta = local_meta.copy()

        if progress_callback:
            progress_callback(f"--- Exporting: {fw_id} ---")

        if is_stm32:
            # STM32: raw copy of single .bin (no encryption)
            src_path = os.path.join(src_dir, FileNames.APP_BIN)
            dest_path = os.path.join(fw_out_dir, FileNames.APP_BIN)

            if os.path.exists(src_path):
                shutil.copy2(src_path, dest_path)
                with open(src_path, "rb") as f:
                    final_md5 = calculate_md5_bytes(f.read())

                export_meta["path"] = f"{fw_id}/{FileNames.APP_BIN}"
                export_meta["md5"] = final_md5
                export_meta["path_bootloader"] = ""
                export_meta["md5_bootloader"] = ""
                export_meta["path_partition"] = ""
                export_meta["md5_partition"] = ""

                if progress_callback:
                    progress_callback(f"  OK: {FileNames.APP_BIN} (Raw)")
            else:
                return False, f"App binary not found: {src_path}"
        else:
            # ESP32: encrypt all files
            from .crypto import pad, AES

            files_map = [
                (FileNames.APP_BIN, FileNames.APP_ENC, "path", "md5"),
                (FileNames.BOOTLOADER_BIN, FileNames.BOOTLOADER_ENC, "path_bootloader", "md5_bootloader"),
                (FileNames.PARTITION_BIN, FileNames.PARTITION_ENC, "path_partition", "md5_partition")
            ]

            for src_name, dest_name, path_key, md5_key in files_map:
                src_path = os.path.join(src_dir, src_name)

                if os.path.exists(src_path):
                    dest_path = os.path.join(fw_out_dir, dest_name)

                    with open(src_path, "rb") as f:
                        raw_data = f.read()

                    # MD5 of original binary — matches device verification after
                    # PKCS7 strip (same contract as sd_card.py).
                    final_md5 = calculate_md5_bytes(raw_data)
                    enc_data = encryptor.encrypt_data(raw_data)  # encrypt handles padding internally
                    with open(dest_path, "wb") as f:
                        f.write(enc_data)

                    export_meta[path_key] = f"{fw_id}/{dest_name}"
                    export_meta[md5_key] = final_md5

                    if progress_callback:
                        progress_callback(f"  OK: {dest_name}")
                else:
                    export_meta[path_key] = ""
                    export_meta[md5_key] = ""

        return True, export_meta

    @staticmethod
    def _read_index(index_path: str, encryptor: Optional[FWEncryptor] = None
                    ) -> List[Dict[str, Any]]:
        """Load index.txt, auto-detecting plaintext (legacy) vs AES-encrypted."""
        if not os.path.isfile(index_path):
            return []
        try:
            with open(index_path, "rb") as f:
                raw = f.read()
        except Exception:
            return []
        # Plaintext JSON (legacy index)?
        try:
            return json.loads(raw.decode("utf-8"))
        except Exception:
            pass
        # Encrypted index?
        if encryptor is not None:
            try:
                return json.loads(encryptor.decrypt_data(raw).decode("utf-8"))
            except Exception:
                pass
        return []

    def update_index(self, firmwares: List[Dict[str, Any]],
                     encryptor: Optional[FWEncryptor] = None,
                     clean_old: bool = True) -> Tuple[bool, str]:
        """
        Update index.txt in release directory.
        PC is source of truth - replaces index entirely.

        Args:
            firmwares: List of firmware metadata (this becomes the new index)
            encryptor: if given, the index is AES-encrypted before writing so
                the firmware catalog is unreadable to anyone browsing the repo
            clean_old: If True, delete old folders not in the new list

        Returns:
            Tuple of (success, message)
        """
        ensure_dir(self.release_dir)
        index_path = os.path.join(self.release_dir, FileNames.INDEX)

        # Get new fw_ids
        new_fw_ids = {fw.get("fw_id") for fw in firmwares if fw.get("fw_id")}

        # Clean up old folders not in new list
        if clean_old:
            for item in os.listdir(self.release_dir):
                item_path = os.path.join(self.release_dir, item)
                # Skip non-directories and special files
                if not os.path.isdir(item_path) or item.startswith("."):
                    continue
                # Delete if not in new list
                if item not in new_fw_ids:
                    try:
                        shutil.rmtree(item_path)
                    except:
                        pass

        # Save new index (replace entirely). Encrypt when an encryptor is
        # given so the catalog is unreadable to anyone browsing the repo.
        try:
            payload = json.dumps(firmwares, indent=2,
                                 ensure_ascii=False).encode("utf-8")
            if encryptor is not None:
                payload = encryptor.encrypt_data(payload)
            with open(index_path, "wb") as f:
                f.write(payload)
            note = " (encrypted)" if encryptor is not None else ""
            return True, f"Index updated ({len(firmwares)} entries){note}"
        except Exception as e:
            return False, f"Failed to save index: {e}"

    def remove_from_index(self, fw_ids: List[str],
                          encryptor: Optional[FWEncryptor] = None
                          ) -> Tuple[bool, str]:
        """
        Remove firmwares from index.txt and delete their folders.

        Args:
            fw_ids: List of firmware IDs to remove
            encryptor: needed to read/rewrite an encrypted index. The folder
                deletion still happens even without it; the index is then
                left for the next full sync to rebuild.

        Returns:
            Tuple of (success, message)
        """
        index_path = os.path.join(self.release_dir, FileNames.INDEX)

        # Load existing index (plaintext or encrypted, auto-detected)
        existing_index = self._read_index(index_path, encryptor)

        # Remove from index
        new_index = [item for item in existing_index
                     if item.get("fw_id") not in fw_ids]

        # Remove folders
        for fw_id in fw_ids:
            fw_dir = os.path.join(self.release_dir, fw_id)
            if os.path.isdir(fw_dir):
                shutil.rmtree(fw_dir)

        # Save updated index in the same (encrypted) format
        try:
            payload = json.dumps(new_index, indent=2,
                                 ensure_ascii=False).encode("utf-8")
            if encryptor is not None:
                payload = encryptor.encrypt_data(payload)
            with open(index_path, "wb") as f:
                f.write(payload)
            return True, f"Removed {len(fw_ids)} firmwares from index"
        except Exception as e:
            return False, f"Failed to update index: {e}"

    def commit_and_push(
        self,
        commit_message: str = "Update firmware",
        progress_callback: Optional[Callable[[str], None]] = None
    ) -> Tuple[bool, str]:
        """
        Commit changes and force push to remote.
        Uses --force like original FlashPorter.pyw.

        Args:
            commit_message: Git commit message
            progress_callback: Optional callback for progress messages

        Returns:
            Tuple of (success, message)
        """
        if not self.is_git_installed():
            return False, "Git is not installed"

        if not self.is_git_repo():
            success, msg = self.init_repo()
            if not success:
                return False, msg

        if not self.repo_url:
            return False, "Repository URL not set"

        def log(msg):
            if progress_callback:
                progress_callback(msg)

        try:
            # Configure remote
            log("Setting remote origin...")
            subprocess.run(
                ["git", "remote", "remove", "origin"],
                cwd=self.release_dir,
                capture_output=True,
                timeout=10
            )
            result = subprocess.run(
                ["git", "remote", "add", "origin", self.repo_url],
                cwd=self.release_dir,
                capture_output=True,
                text=True,
                timeout=10
            )

            # Stage all changes
            log("Staging changes...")
            result = subprocess.run(
                ["git", "add", "-A"],
                cwd=self.release_dir,
                capture_output=True,
                text=True,
                timeout=30
            )

            # Check if there are changes to commit
            result = subprocess.run(
                ["git", "status", "--porcelain"],
                cwd=self.release_dir,
                capture_output=True,
                text=True,
                timeout=10
            )
            if not result.stdout.strip():
                return True, "No changes to commit"

            # Commit
            log("Committing...")
            result = subprocess.run(
                ["git", "commit", "-m", commit_message],
                cwd=self.release_dir,
                capture_output=True,
                text=True,
                timeout=30
            )

            # Push with --force (like original FlashPorter.pyw)
            log("Pushing to remote...")
            result = subprocess.run(
                ["git", "push", "-u", "origin", "main", "--force"],
                cwd=self.release_dir,
                capture_output=True,
                text=True,
                timeout=120
            )

            if result.returncode == 0:
                return True, "Push successful"
            else:
                return False, f"Push failed: {result.stderr}"

        except subprocess.TimeoutExpired:
            return False, "Git operation timed out"
        except Exception as e:
            return False, f"Git error: {e}"

    def create_push_script(self) -> str:
        """
        Create a batch script for a fresh-history push (opens terminal).

        Wipes the local .git dir and re-inits so remote history is replaced
        with exactly one commit reflecting current local state.  This makes
        the PC the definitive source of truth and removes stale firmware blobs
        from remote history without needing BFG / filter-branch.

        Returns:
            Path to the created script
        """
        if not self.repo_url:
            raise ValueError("Repository URL not set")

        script_path = os.path.join(self.release_dir, "push_to_git.bat")

        # Format URL with username if needed
        final_url = self.repo_url
        if "https://" in self.repo_url and "@" not in self.repo_url:
            try:
                parts = self.repo_url.split("github.com/")
                if len(parts) > 1:
                    user_part = parts[1].split("/")[0]
                    final_url = self.repo_url.replace("https://", f"https://{user_part}@")
            except:
                pass

        script_content = f'''@echo off
title Push Firmware to Git
color 0A
echo ==========================================
echo  SYNC FIRMWARE TO GIT  (fresh push)
echo ==========================================
echo Repo: {final_url}
echo.
cd /d "{self.release_dir}"

echo [Clean history - PC is source of truth]...
if exist .git rmdir /s /q .git

echo [Init fresh repo]...
git init
git branch -M main

echo [Git identity (required for commit)]...
git config user.email "flashporter@local"
git config user.name "FlashPorter"

echo [Stage all files]...
git add -A

echo [Commit]...
git commit -m "Firmware release - FlashPorter"

echo [Set remote]...
git remote add origin {final_url}

echo [Push - force overwrite remote history]...
git push -u origin main --force

echo.
if %ERRORLEVEL% EQU 0 (
    color 0A
    echo SUCCESS - Remote now matches local library exactly.
) else (
    color 0C
    echo FAILED - Check credentials or network, then re-run this script.
)
echo.
pause
'''

        with open(script_path, "w") as f:
            f.write(script_content)

        return script_path


# Quick test
if __name__ == "__main__":
    git = GitManager()
    print(f"Git installed: {git.is_git_installed()}")
    print(f"Git version: {git.get_git_version()}")
    print(f"Release dir: {git.release_dir}")
    print(f"Is repo: {git.is_git_repo()}")
