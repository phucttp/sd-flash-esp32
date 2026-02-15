"""
auth.py - Local Authentication Module
=====================================
Handles user login without server connection.
Uses PBKDF2-SHA256 for secure password hashing.

Security Model:
- First run: User creates account (username + password)
- Password is hashed with random salt using PBKDF2 (100,000 iterations)
- Only hash + salt stored, never plain password
- Login: Compare hash(input + stored_salt) with stored_hash
"""

import os
import json
import hashlib
import secrets
from typing import Optional, Tuple

# PBKDF2 iterations - higher = more secure but slower
PBKDF2_ITERATIONS = 100_000
SALT_LENGTH = 32  # bytes
HASH_LENGTH = 64  # bytes


class AuthManager:
    """Manages local user authentication."""

    def __init__(self, credentials_file: str = "credentials.json"):
        """
        Initialize auth manager.

        Args:
            credentials_file: Path to store hashed credentials
        """
        self.credentials_file = credentials_file
        self._credentials = None

    def _get_app_dir(self) -> str:
        """Get application directory."""
        import sys
        if getattr(sys, 'frozen', False):
            return os.path.dirname(sys.executable)
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    def _get_credentials_path(self) -> str:
        """Get full path to credentials file."""
        return os.path.join(self._get_app_dir(), self.credentials_file)

    def _hash_password(self, password: str, salt: Optional[bytes] = None) -> Tuple[str, str]:
        """
        Hash password using PBKDF2-SHA256.

        Args:
            password: Plain text password
            salt: Optional salt bytes (generated if not provided)

        Returns:
            Tuple of (hash_hex, salt_hex)
        """
        if salt is None:
            salt = secrets.token_bytes(SALT_LENGTH)

        # PBKDF2 with SHA256
        dk = hashlib.pbkdf2_hmac(
            'sha256',
            password.encode('utf-8'),
            salt,
            PBKDF2_ITERATIONS,
            dklen=HASH_LENGTH
        )

        return dk.hex(), salt.hex()

    def _verify_password(self, password: str, stored_hash: str, salt_hex: str) -> bool:
        """
        Verify password against stored hash.

        Args:
            password: Plain text password to verify
            stored_hash: Stored hash (hex)
            salt_hex: Stored salt (hex)

        Returns:
            True if password matches
        """
        salt = bytes.fromhex(salt_hex)
        computed_hash, _ = self._hash_password(password, salt)

        # Constant-time comparison to prevent timing attacks
        return secrets.compare_digest(computed_hash, stored_hash)

    def is_first_run(self) -> bool:
        """Check if this is the first run (no credentials exist)."""
        path = self._get_credentials_path()
        return not os.path.exists(path)

    def create_account(self, username: str, password: str) -> Tuple[bool, str]:
        """
        Create new account (first run only).

        Args:
            username: Username to create
            password: Password to set

        Returns:
            Tuple of (success, message)
        """
        # Validation
        if len(username.strip()) < 3:
            return False, "Username must be at least 3 characters"

        if len(password) < 6:
            return False, "Password must be at least 6 characters"

        # Check if account already exists
        if not self.is_first_run():
            return False, "Account already exists. Use change_password() instead."

        # Hash password
        password_hash, salt = self._hash_password(password)

        # Save credentials
        credentials = {
            "username": username.strip(),
            "password_hash": password_hash,
            "salt": salt,
            "version": 1,  # For future migration
            "created_at": self._get_timestamp()
        }

        try:
            with open(self._get_credentials_path(), 'w', encoding='utf-8') as f:
                json.dump(credentials, f, indent=2)
            return True, "Account created successfully"
        except Exception as e:
            return False, f"Failed to save credentials: {e}"

    def login(self, username: str, password: str) -> Tuple[bool, str]:
        """
        Verify login credentials.

        Args:
            username: Username to verify
            password: Password to verify

        Returns:
            Tuple of (success, message)
        """
        if self.is_first_run():
            return False, "No account exists. Please create one first."

        try:
            with open(self._get_credentials_path(), 'r', encoding='utf-8') as f:
                creds = json.load(f)
        except Exception as e:
            return False, f"Failed to read credentials: {e}"

        # Check username (case-insensitive)
        if creds.get("username", "").lower() != username.strip().lower():
            return False, "Invalid username or password"

        # Verify password
        if not self._verify_password(password, creds["password_hash"], creds["salt"]):
            return False, "Invalid username or password"

        # Update last login
        creds["last_login"] = self._get_timestamp()
        try:
            with open(self._get_credentials_path(), 'w', encoding='utf-8') as f:
                json.dump(creds, f, indent=2)
        except:
            pass  # Non-critical

        return True, f"Welcome, {creds['username']}!"

    def change_password(self, current_password: str, new_password: str) -> Tuple[bool, str]:
        """
        Change password (requires current password).

        Args:
            current_password: Current password for verification
            new_password: New password to set

        Returns:
            Tuple of (success, message)
        """
        if self.is_first_run():
            return False, "No account exists"

        try:
            with open(self._get_credentials_path(), 'r', encoding='utf-8') as f:
                creds = json.load(f)
        except Exception as e:
            return False, f"Failed to read credentials: {e}"

        # Verify current password
        if not self._verify_password(current_password, creds["password_hash"], creds["salt"]):
            return False, "Current password is incorrect"

        # Validate new password
        if len(new_password) < 6:
            return False, "New password must be at least 6 characters"

        # Hash new password with new salt
        password_hash, salt = self._hash_password(new_password)

        # Update credentials
        creds["password_hash"] = password_hash
        creds["salt"] = salt
        creds["updated_at"] = self._get_timestamp()

        try:
            with open(self._get_credentials_path(), 'w', encoding='utf-8') as f:
                json.dump(creds, f, indent=2)
            return True, "Password changed successfully"
        except Exception as e:
            return False, f"Failed to save new password: {e}"

    def get_username(self) -> Optional[str]:
        """Get stored username (for display after login)."""
        try:
            with open(self._get_credentials_path(), 'r', encoding='utf-8') as f:
                creds = json.load(f)
            return creds.get("username")
        except:
            return None

    def reset_account(self, master_key: str = "FLASHPORTER_RESET_2024") -> Tuple[bool, str]:
        """
        Reset account (requires master key).
        This is a recovery option if user forgets password.

        Args:
            master_key: Master recovery key

        Returns:
            Tuple of (success, message)
        """
        # Simple master key check - you can customize this
        expected_key = "FLASHPORTER_RESET_2024"

        if master_key != expected_key:
            return False, "Invalid master key"

        path = self._get_credentials_path()
        if os.path.exists(path):
            try:
                os.remove(path)
                return True, "Account reset. Please create a new account."
            except Exception as e:
                return False, f"Failed to reset: {e}"

        return True, "No account to reset"

    def _get_timestamp(self) -> str:
        """Get current timestamp string."""
        from datetime import datetime
        return datetime.now().isoformat()


# Quick test
if __name__ == "__main__":
    auth = AuthManager("test_credentials.json")

    if auth.is_first_run():
        print("First run - creating account...")
        success, msg = auth.create_account("admin", "password123")
        print(f"Create: {success} - {msg}")

    print("\nTesting login...")
    success, msg = auth.login("admin", "password123")
    print(f"Login: {success} - {msg}")

    success, msg = auth.login("admin", "wrongpassword")
    print(f"Wrong pass: {success} - {msg}")

    # Cleanup test file
    if os.path.exists("test_credentials.json"):
        os.remove("test_credentials.json")
