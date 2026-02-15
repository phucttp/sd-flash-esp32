"""
firmware_lib.py - Firmware Library Management
==============================================
Manages local firmware library (add, list, delete, update).
"""

import os
import json
import shutil
from typing import List, Dict, Tuple, Optional, Any

from .utils import (
    get_app_dir, safe_name, calculate_md5,
    ensure_dir, FileNames, generate_short_fw_id
)


class FirmwareLibrary:
    """Manages local firmware library."""

    def __init__(self, lib_root: Optional[str] = None):
        """
        Initialize firmware library manager.

        Args:
            lib_root: Root directory for firmware library.
                     Default: <app_dir>/firmware_library
        """
        if lib_root is None:
            lib_root = os.path.join(get_app_dir(), "firmware_library")
        self.lib_root = lib_root
        ensure_dir(self.lib_root)

    def list_firmwares(self) -> List[Dict[str, Any]]:
        """
        List all firmwares in library.

        Returns:
            List of dictionaries with firmware info
        """
        items = []

        for name in os.listdir(self.lib_root):
            path = os.path.join(self.lib_root, name)
            meta_path = os.path.join(path, FileNames.METADATA)

            if os.path.isdir(path) and os.path.isfile(meta_path):
                try:
                    with open(meta_path, "r", encoding="utf-8") as f:
                        meta = json.load(f)

                    # Add path info
                    meta["_path"] = path
                    meta["_folder_name"] = name

                    items.append(meta)
                except Exception as e:
                    print(f"Could not load metadata for {name}: {e}")

        # Sort by fw_id
        items.sort(key=lambda x: x.get("fw_id", ""))
        return items

    def get_firmware(self, fw_id: str) -> Optional[Dict[str, Any]]:
        """
        Get firmware metadata by ID.

        Args:
            fw_id: Firmware ID

        Returns:
            Firmware metadata dict or None if not found
        """
        path = os.path.join(self.lib_root, fw_id)
        meta_path = os.path.join(path, FileNames.METADATA)

        if not os.path.isfile(meta_path):
            return None

        try:
            with open(meta_path, "r", encoding="utf-8") as f:
                meta = json.load(f)
            meta["_path"] = path
            meta["_folder_name"] = fw_id
            return meta
        except:
            return None

    def add_firmware(
        self,
        fw_id: str,
        device_type: str,
        version: str,
        app_path: str,
        boot_path: str,
        part_path: str,
        description: str = "",
        overwrite: bool = False
    ) -> Tuple[bool, str]:
        """
        Add firmware to library.

        Args:
            fw_id: Unique firmware ID
            device_type: Device type name
            version: Version string
            app_path: Path to application binary
            boot_path: Path to bootloader binary
            part_path: Path to partition table binary
            description: Firmware description (shown on OLED Status tab)
            overwrite: Whether to overwrite existing

        Returns:
            Tuple of (success, message)
        """
        # Sanitize fw_id
        fw_id = safe_name(fw_id)
        if not fw_id:
            return False, "Invalid firmware ID"

        # Validate inputs
        if not all([device_type.strip(), version.strip()]):
            return False, "Device type and version are required"

        # Detect target type from device_type
        is_stm32 = "STM32" in device_type.upper()

        if not os.path.isfile(app_path):
            return False, "Application binary file is required"

        # ESP32 requires all 3 files, STM32 only needs app
        if not is_stm32:
            if not all([
                os.path.isfile(boot_path),
                os.path.isfile(part_path)
            ]):
                return False, "ESP32 requires bootloader and partition files"

        dest = os.path.join(self.lib_root, fw_id)

        # Handle existing firmware
        if os.path.exists(dest):
            if not overwrite:
                return False, f"Firmware '{fw_id}' already exists"
            shutil.rmtree(dest)

        os.makedirs(dest, exist_ok=True)

        try:
            # Copy app binary (always required)
            dest_app = os.path.join(dest, FileNames.APP_BIN)
            shutil.copy2(app_path, dest_app)
            md5_app = calculate_md5(dest_app)
            if not md5_app:
                raise Exception("Failed to calculate MD5 for app binary")

            # Copy bootloader & partition (ESP32 only)
            md5_boot = ""
            md5_part = ""
            if not is_stm32 and boot_path and os.path.isfile(boot_path):
                dest_boot = os.path.join(dest, FileNames.BOOTLOADER_BIN)
                shutil.copy2(boot_path, dest_boot)
                md5_boot = calculate_md5(dest_boot) or ""

            if not is_stm32 and part_path and os.path.isfile(part_path):
                dest_part = os.path.join(dest, FileNames.PARTITION_BIN)
                shutil.copy2(part_path, dest_part)
                md5_part = calculate_md5(dest_part) or ""

            # Create metadata
            metadata = {
                "fw_id": fw_id,
                "device_type": device_type.strip(),
                "version": version.strip(),
                "description": description.strip(),
                "path": f"{fw_id}/{FileNames.APP_BIN}",
                "md5": md5_app,
                "path_bootloader": f"{fw_id}/{FileNames.BOOTLOADER_BIN}" if md5_boot else "",
                "md5_bootloader": md5_boot,
                "path_partition": f"{fw_id}/{FileNames.PARTITION_BIN}" if md5_part else "",
                "md5_partition": md5_part
            }

            # Save metadata
            meta_path = os.path.join(dest, FileNames.METADATA)
            with open(meta_path, "w", encoding="utf-8") as f:
                json.dump(metadata, f, indent=2, ensure_ascii=False)

            return True, f"Firmware '{fw_id}' added successfully"

        except Exception as e:
            # Cleanup on error
            if os.path.exists(dest):
                shutil.rmtree(dest)
            return False, f"Failed to add firmware: {e}"

    def delete_firmware(self, fw_id: str) -> Tuple[bool, str]:
        """
        Delete firmware from library.

        Args:
            fw_id: Firmware ID to delete

        Returns:
            Tuple of (success, message)
        """
        path = os.path.join(self.lib_root, fw_id)

        if not os.path.exists(path):
            return False, f"Firmware '{fw_id}' not found"

        try:
            shutil.rmtree(path)
            return True, f"Firmware '{fw_id}' deleted"
        except Exception as e:
            return False, f"Failed to delete: {e}"

    def update_metadata(self, fw_id: str, updates: Dict[str, Any]) -> Tuple[bool, str]:
        """
        Update firmware metadata fields.

        Args:
            fw_id: Firmware ID
            updates: Dictionary of fields to update

        Returns:
            Tuple of (success, message)
        """
        meta_path = os.path.join(self.lib_root, fw_id, FileNames.METADATA)

        if not os.path.isfile(meta_path):
            return False, f"Firmware '{fw_id}' not found"

        try:
            with open(meta_path, "r", encoding="utf-8") as f:
                meta = json.load(f)

            meta.update(updates)

            with open(meta_path, "w", encoding="utf-8") as f:
                json.dump(meta, f, indent=2, ensure_ascii=False)

            return True, "Metadata updated"
        except Exception as e:
            return False, f"Failed to update: {e}"

    def get_suggestions(self) -> Tuple[List[str], List[str]]:
        """
        Get suggestions for fw_id and device_type from existing library.

        Returns:
            Tuple of (list of fw_ids, list of device_types)
        """
        fw_ids = []
        device_types = set()

        for name in os.listdir(self.lib_root):
            path = os.path.join(self.lib_root, name)
            if os.path.isdir(path):
                fw_ids.append(name)

                meta_path = os.path.join(path, FileNames.METADATA)
                if os.path.isfile(meta_path):
                    try:
                        with open(meta_path, "r", encoding="utf-8") as f:
                            data = json.load(f)
                        if "device_type" in data:
                            device_types.add(data["device_type"])
                    except:
                        pass

        return sorted(fw_ids), sorted(list(device_types))

    def generate_next_id(self) -> str:
        """Generate next available firmware ID."""
        return generate_short_fw_id(self.lib_root)

    def get_firmware_files(self, fw_id: str) -> Dict[str, Optional[str]]:
        """
        Get paths to firmware files.

        Args:
            fw_id: Firmware ID

        Returns:
            Dict with paths to app, bootloader, partition files
        """
        base = os.path.join(self.lib_root, fw_id)
        result = {
            "app": None,
            "bootloader": None,
            "partition": None
        }

        app_path = os.path.join(base, FileNames.APP_BIN)
        boot_path = os.path.join(base, FileNames.BOOTLOADER_BIN)
        part_path = os.path.join(base, FileNames.PARTITION_BIN)

        if os.path.isfile(app_path):
            result["app"] = app_path
        if os.path.isfile(boot_path):
            result["bootloader"] = boot_path
        if os.path.isfile(part_path):
            result["partition"] = part_path

        return result


# Quick test
if __name__ == "__main__":
    lib = FirmwareLibrary()
    print(f"Library root: {lib.lib_root}")
    print(f"Firmwares: {len(lib.list_firmwares())}")
    print(f"Next ID: {lib.generate_next_id()}")

    ids, types = lib.get_suggestions()
    print(f"Existing IDs: {ids}")
    print(f"Device types: {types}")
