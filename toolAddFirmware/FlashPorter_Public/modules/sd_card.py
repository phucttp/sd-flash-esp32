"""
sd_card.py - SD Card Operations Module
=======================================
Handles copying firmware to SD card (plain and encrypted).
"""

import os
import json
import shutil
from typing import List, Dict, Tuple, Any, Optional, Callable

from .utils import FileNames, calculate_md5_bytes, ensure_dir
from .crypto import FWEncryptor, load_crypto


class SDCardManager:
    """Manages SD card firmware operations."""

    def __init__(self, sd_path: str = ""):
        """
        Initialize SD card manager.

        Args:
            sd_path: Path to SD card mount point
        """
        self.sd_path = sd_path
        self.index_data: List[Dict[str, Any]] = []

    def set_path(self, path: str) -> bool:
        """
        Set SD card path.

        Args:
            path: Path to SD card mount point

        Returns:
            True if path is valid
        """
        if path and os.path.isdir(path):
            self.sd_path = path
            return True
        return False

    def is_valid(self) -> bool:
        """Check if SD card path is valid."""
        return bool(self.sd_path) and os.path.isdir(self.sd_path)

    def load_index(self) -> Tuple[bool, str]:
        """
        Load index.txt from SD card.

        Returns:
            Tuple of (success, message)
        """
        if not self.is_valid():
            return False, "Invalid SD card path"

        index_path = os.path.join(self.sd_path, FileNames.INDEX)
        self.index_data = []

        if not os.path.isfile(index_path):
            return True, "Index file not found (SD card may be empty)"

        try:
            with open(index_path, "r", encoding="utf-8") as f:
                self.index_data = json.load(f)

            if not isinstance(self.index_data, list):
                raise Exception("Index file is not a JSON array")

            return True, f"Loaded {len(self.index_data)} entries"

        except Exception as e:
            self.index_data = []
            return False, f"Failed to load index: {e}"

    def save_index(self) -> Tuple[bool, str]:
        """
        Save index.txt to SD card.

        Returns:
            Tuple of (success, message)
        """
        if not self.is_valid():
            return False, "Invalid SD card path"

        index_path = os.path.join(self.sd_path, FileNames.INDEX)

        try:
            with open(index_path, "w", encoding="utf-8") as f:
                json.dump(self.index_data, f, indent=2, ensure_ascii=False)
            return True, "Index saved"
        except Exception as e:
            return False, f"Failed to save index: {e}"

    def get_firmware_list(self) -> List[str]:
        """Get list of firmware IDs on SD card."""
        return [item.get("fw_id", "") for item in self.index_data]

    def get_firmware_meta(self, fw_id: str) -> Optional[Dict[str, Any]]:
        """Get metadata for a firmware on SD card."""
        for item in self.index_data:
            if item.get("fw_id") == fw_id:
                return item
        return None

    @staticmethod
    def _is_stm32(meta: Dict[str, Any]) -> bool:
        """Check if firmware targets STM32 based on device_type."""
        return "STM32" in meta.get("device_type", "").upper()

    def copy_plain(
        self,
        fw_id: str,
        src_dir: str,
        local_meta: Dict[str, Any],
        progress_callback: Optional[Callable[[str], None]] = None
    ) -> Tuple[bool, str]:
        """
        Copy firmware to SD card (plain, with padding for ESP32).
        STM32 firmware is copied raw (no padding).

        Args:
            fw_id: Firmware ID
            src_dir: Source directory containing firmware files
            local_meta: Local metadata for the firmware
            progress_callback: Optional callback for progress messages

        Returns:
            Tuple of (success, message)
        """
        if not self.is_valid():
            return False, "Invalid SD card path"

        is_stm32 = self._is_stm32(local_meta)

        # ESP32 needs crypto for padding, STM32 does not
        if not is_stm32:
            if not load_crypto():
                return False, "Crypto library not available (needed for padding)"

        dest_dir = os.path.join(self.sd_path, fw_id)

        # Remove existing and create new
        if os.path.exists(dest_dir):
            shutil.rmtree(dest_dir)
        os.makedirs(dest_dir, exist_ok=True)

        # Create SD metadata (will modify MD5 to padded version)
        sd_meta = local_meta.copy()

        if is_stm32:
            # STM32: raw copy of single .bin file (no padding)
            src_file = os.path.join(src_dir, FileNames.APP_BIN)
            dest_file = os.path.join(dest_dir, FileNames.APP_BIN)

            if os.path.exists(src_file):
                shutil.copy2(src_file, dest_file)
                final_md5 = calculate_md5_bytes(open(src_file, "rb").read())
                sd_meta["path"] = f"/{fw_id}/{FileNames.APP_BIN}"
                sd_meta["md5"] = final_md5
                sd_meta["path_bootloader"] = ""
                sd_meta["md5_bootloader"] = ""
                sd_meta["path_partition"] = ""
                sd_meta["md5_partition"] = ""

                if progress_callback:
                    progress_callback(f"  -> {FileNames.APP_BIN}: OK (Raw)")
            else:
                return False, f"App binary not found: {src_file}"
        else:
            # ESP32: pad all files to AES block size
            from .crypto import pad, AES

            files_map = [
                (FileNames.APP_BIN, FileNames.APP_BIN, "path", "md5"),
                (FileNames.BOOTLOADER_BIN, FileNames.BOOTLOADER_BIN, "path_bootloader", "md5_bootloader"),
                (FileNames.PARTITION_BIN, FileNames.PARTITION_BIN, "path_partition", "md5_partition")
            ]

            for src_name, dest_name, path_key, md5_key in files_map:
                src_file = os.path.join(src_dir, src_name)
                dest_file = os.path.join(dest_dir, dest_name)

                if os.path.exists(src_file):
                    with open(src_file, "rb") as f:
                        raw_data = f.read()

                    padded_data = pad(raw_data, AES.block_size)

                    with open(dest_file, "wb") as f:
                        f.write(padded_data)

                    final_md5 = calculate_md5_bytes(padded_data)
                    sd_meta[path_key] = f"/{fw_id}/{dest_name}"
                    sd_meta[md5_key] = final_md5

                    if progress_callback:
                        progress_callback(f"  -> {dest_name}: OK (Padded)")
                else:
                    sd_meta[path_key] = ""
                    sd_meta[md5_key] = ""

        # Update index
        self.index_data = [i for i in self.index_data if i.get("fw_id") != fw_id]
        self.index_data.append(sd_meta)

        return True, f"Copied '{fw_id}' to SD card (plain)"

    def copy_encrypted(
        self,
        fw_id: str,
        src_dir: str,
        local_meta: Dict[str, Any],
        encryptor: FWEncryptor,
        progress_callback: Optional[Callable[[str], None]] = None
    ) -> Tuple[bool, str]:
        """
        Copy firmware to SD card (encrypted).

        Args:
            fw_id: Firmware ID
            src_dir: Source directory containing firmware files
            local_meta: Local metadata for the firmware
            encryptor: FWEncryptor instance with key/IV
            progress_callback: Optional callback for progress messages

        Returns:
            Tuple of (success, message)
        """
        if not self.is_valid():
            return False, "Invalid SD card path"

        # STM32 does not support encryption
        if self._is_stm32(local_meta):
            return False, "STM32 firmware does not support encryption. Use 'Copy Plain' instead."

        if not load_crypto():
            return False, "Crypto library not available"

        from .crypto import pad, AES

        dest_dir = os.path.join(self.sd_path, fw_id)

        # Remove existing and create new
        if os.path.exists(dest_dir):
            shutil.rmtree(dest_dir)
        os.makedirs(dest_dir, exist_ok=True)

        # Create SD metadata
        sd_meta = local_meta.copy()

        # File mapping: (source_name, dest_name, path_key, md5_key)
        files_map = [
            (FileNames.APP_BIN, FileNames.APP_ENC, "path", "md5"),
            (FileNames.BOOTLOADER_BIN, FileNames.BOOTLOADER_ENC, "path_bootloader", "md5_bootloader"),
            (FileNames.PARTITION_BIN, FileNames.PARTITION_ENC, "path_partition", "md5_partition")
        ]

        for src_name, dest_name, path_key, md5_key in files_map:
            src_file = os.path.join(src_dir, src_name)
            dest_file = os.path.join(dest_dir, dest_name)

            if os.path.exists(src_file):
                # Read raw data
                with open(src_file, "rb") as f:
                    raw_data = f.read()

                # Calculate MD5 of padded data (for ESP32 verification after decrypt)
                padded_data = pad(raw_data, AES.block_size)
                final_md5 = calculate_md5_bytes(padded_data)

                # Encrypt and write
                enc_data = encryptor.encrypt_data(raw_data)
                with open(dest_file, "wb") as f:
                    f.write(enc_data)

                # Update SD metadata (path to .enc, MD5 of padded data)
                sd_meta[path_key] = f"/{fw_id}/{dest_name}"
                sd_meta[md5_key] = final_md5

                if progress_callback:
                    progress_callback(f"  -> {dest_name}: OK (Encrypted)")
            else:
                sd_meta[path_key] = ""
                sd_meta[md5_key] = ""

        # Mark as encrypted
        sd_meta["encrypted"] = True

        # Update index
        self.index_data = [i for i in self.index_data if i.get("fw_id") != fw_id]
        self.index_data.append(sd_meta)

        return True, f"Copied '{fw_id}' to SD card (encrypted)"

    def remove_firmware(self, fw_id: str) -> Tuple[bool, str]:
        """
        Remove firmware from SD card.

        Args:
            fw_id: Firmware ID to remove

        Returns:
            Tuple of (success, message)
        """
        if not self.is_valid():
            return False, "Invalid SD card path"

        dest_dir = os.path.join(self.sd_path, fw_id)

        # Remove directory if exists
        if os.path.isdir(dest_dir):
            try:
                shutil.rmtree(dest_dir)
            except Exception as e:
                return False, f"Failed to remove directory: {e}"

        # Remove from index
        original_count = len(self.index_data)
        self.index_data = [i for i in self.index_data if i.get("fw_id") != fw_id]

        return True, f"Removed '{fw_id}' from SD card"


# Quick test
if __name__ == "__main__":
    manager = SDCardManager()
    print(f"Is valid: {manager.is_valid()}")

    # Test with a path (if available)
    test_path = "D:/"  # Change to your SD card path
    if os.path.isdir(test_path):
        manager.set_path(test_path)
        success, msg = manager.load_index()
        print(f"Load index: {success} - {msg}")
        print(f"Firmware list: {manager.get_firmware_list()}")
