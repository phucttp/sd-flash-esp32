"""
utils.py - Utility Functions
=============================
Common helper functions used across the application.
"""

import os
import sys
import hashlib
import time
from typing import Optional


def get_app_dir() -> str:
    """
    Get the application directory.
    Works for both script and frozen (PyInstaller) execution.
    """
    if getattr(sys, 'frozen', False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def safe_name(name: str) -> str:
    """
    Sanitize folder/file name for FAT32 compatibility.

    Args:
        name: Raw name string

    Returns:
        Sanitized name (only alphanumeric, dash, underscore, dot)
    """
    s = name.strip().replace(" ", "_")
    allowed = "-_.0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    return "".join(c for c in s if c in allowed)


def calculate_md5(filepath: str) -> Optional[str]:
    """
    Calculate MD5 hash of a file.

    Args:
        filepath: Path to file

    Returns:
        MD5 hex string or None if error
    """
    hash_md5 = hashlib.md5()
    try:
        with open(filepath, "rb") as f:
            for chunk in iter(lambda: f.read(4096), b""):
                hash_md5.update(chunk)
        return hash_md5.hexdigest()
    except Exception as e:
        print(f"Error calculating MD5 for {filepath}: {e}")
        return None


def calculate_md5_bytes(data: bytes) -> str:
    """
    Calculate MD5 hash of bytes.

    Args:
        data: Bytes to hash

    Returns:
        MD5 hex string
    """
    return hashlib.md5(data).hexdigest()


def generate_short_fw_id(lib_root: str) -> str:
    """
    Generate short firmware ID (FAT32 8.3 compatible).
    Format: FW001, FW002, ... FW999

    Args:
        lib_root: Path to firmware library root

    Returns:
        Unique firmware ID
    """
    existing = set()

    if os.path.exists(lib_root):
        for name in os.listdir(lib_root):
            if name.upper().startswith("FW") and len(name) <= 5:
                existing.add(name.upper())

    # Find next available ID (FW001 -> FW999)
    for i in range(1, 1000):
        fw_id = f"FW{i:03d}"
        if fw_id not in existing:
            return fw_id

    # Fallback: use timestamp
    return f"FW{int(time.time()) % 1000:03d}"


def shorten_device_type(name: str) -> str:
    """
    Shorten device type name for display.
    Example: GAP_THU_DIEN_DUNG -> GTDD

    Args:
        name: Full device type name

    Returns:
        Abbreviated name (max 8 chars)
    """
    if not name:
        return ""
    parts = name.replace("-", "_").split("_")
    abbrev = "".join(p[0].upper() for p in parts if p)
    return abbrev[:8]


def shorten_version(ver: str) -> str:
    """
    Shorten version string for display.
    Example: v1.1.2_XXX_H -> v1.1_H

    Args:
        ver: Full version string

    Returns:
        Shortened version
    """
    if not ver:
        return ""

    import re

    # Keep V or H suffix if present
    suffix = ""
    if ver and ver[-1].upper() in ('V', 'H'):
        suffix = "_" + ver[-1].upper()

    # Find vX.Y pattern
    match = re.match(r'(v?\d+\.\d+)', ver, re.IGNORECASE)
    if match:
        result = match.group(1)
        if not result.lower().startswith('v'):
            result = 'v' + result
        return result + suffix

    return ver[:5] + suffix


def format_file_size(size_bytes: int) -> str:
    """
    Format file size for human-readable display.

    Args:
        size_bytes: Size in bytes

    Returns:
        Formatted string (e.g., "1.5 MB")
    """
    for unit in ['B', 'KB', 'MB', 'GB']:
        if size_bytes < 1024:
            return f"{size_bytes:.1f} {unit}"
        size_bytes /= 1024
    return f"{size_bytes:.1f} TB"


def ensure_dir(path: str) -> bool:
    """
    Ensure directory exists, create if not.

    Args:
        path: Directory path

    Returns:
        True if directory exists/created, False on error
    """
    try:
        os.makedirs(path, exist_ok=True)
        return True
    except Exception as e:
        print(f"Error creating directory {path}: {e}")
        return False


def is_valid_path(path: str) -> bool:
    """Check if path is valid (exists and is a directory)."""
    return bool(path) and os.path.isdir(path)


def get_timestamp() -> str:
    """Get current timestamp string."""
    from datetime import datetime
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def get_date_string() -> str:
    """Get current date string (YYMMDD format)."""
    from datetime import datetime
    return datetime.now().strftime("%y%m%d")


# Constants for file names
class FileNames:
    """Standard file names used in the application."""
    APP_BIN = "FW.bin"
    APP_ENC = "FW.enc"
    BOOTLOADER_BIN = "BOTL.bin"
    BOOTLOADER_ENC = "BOTL.enc"
    PARTITION_BIN = "PART.bin"
    PARTITION_ENC = "PART.enc"
    METADATA = "fw_metadata.json"
    INDEX = "index.txt"


# Quick test
if __name__ == "__main__":
    print(f"App dir: {get_app_dir()}")
    print(f"Safe name: {safe_name('Test File (1)')}")
    print(f"Short device: {shorten_device_type('VENDING_MINI_DIEN_DUNG')}")
    print(f"Short version: {shorten_version('v1.2.3_ABCD_H')}")
    print(f"Format size: {format_file_size(1536000)}")
    print(f"Timestamp: {get_timestamp()}")
