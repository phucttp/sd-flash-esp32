"""
crypto.py - AES Encryption Module
==================================
Handles AES-128-CBC encryption for firmware files.
Compatible with ESP32 mbedtls decryption.

Encryption Flow:
1. Read raw firmware binary
2. Pad to 16-byte boundary (PKCS7)
3. Encrypt with AES-128-CBC
4. Output .enc file
"""

from typing import Optional, Tuple

# Lazy load crypto libraries
_crypto_loaded = False
AES = None
pad = None
unpad = None


def load_crypto() -> bool:
    """
    Lazy load PyCryptodome library.
    Returns True if successful, False otherwise.
    """
    global _crypto_loaded, AES, pad, unpad

    if _crypto_loaded:
        return True

    try:
        from Crypto.Cipher import AES as _AES
        from Crypto.Util.Padding import pad as _pad, unpad as _unpad
        AES = _AES
        pad = _pad
        unpad = _unpad
        _crypto_loaded = True
        return True
    except ImportError:
        return False


class FWEncryptor:
    """
    Firmware encryptor using AES-128-CBC.
    Compatible with ESP32 mbedtls decryption.
    """

    BLOCK_SIZE = 16  # AES block size

    def __init__(self, key: str, iv: str):
        """
        Initialize encryptor with key and IV.

        Args:
            key: 16-character AES key
            iv: 16-character initialization vector

        Raises:
            ValueError: If key or IV is invalid length
            ImportError: If PyCryptodome is not installed
        """
        if not load_crypto():
            raise ImportError(
                "PyCryptodome is required. Install with: pip install pycryptodome"
            )

        # Validate key length
        if len(key) != 16:
            raise ValueError(f"Key must be exactly 16 characters, got {len(key)}")
        if len(iv) != 16:
            raise ValueError(f"IV must be exactly 16 characters, got {len(iv)}")

        self.key = key.encode('utf-8')
        self.iv = iv.encode('utf-8')

    def encrypt_data(self, data: bytes) -> bytes:
        """
        Encrypt binary data using AES-128-CBC.

        Args:
            data: Raw binary data to encrypt

        Returns:
            Encrypted data (padded and encrypted)
        """
        # Pad data to 16-byte boundary
        padded_data = pad(data, self.BLOCK_SIZE)

        # Create cipher and encrypt
        cipher = AES.new(self.key, AES.MODE_CBC, self.iv)
        encrypted = cipher.encrypt(padded_data)

        return encrypted

    def decrypt_data(self, encrypted_data: bytes) -> bytes:
        """
        Decrypt AES-128-CBC encrypted data.

        Args:
            encrypted_data: Encrypted binary data

        Returns:
            Decrypted data (with padding removed)
        """
        cipher = AES.new(self.key, AES.MODE_CBC, self.iv)
        decrypted_padded = cipher.decrypt(encrypted_data)

        # Remove padding
        decrypted = unpad(decrypted_padded, self.BLOCK_SIZE)

        return decrypted

    def encrypt_file(self, input_path: str, output_path: str) -> Tuple[bool, str]:
        """
        Encrypt a file and save to output path.

        Args:
            input_path: Path to input file
            output_path: Path to save encrypted file

        Returns:
            Tuple of (success, message)
        """
        try:
            with open(input_path, 'rb') as f:
                data = f.read()

            encrypted = self.encrypt_data(data)

            with open(output_path, 'wb') as f:
                f.write(encrypted)

            return True, f"Encrypted {len(data)} bytes -> {len(encrypted)} bytes"

        except FileNotFoundError:
            return False, f"Input file not found: {input_path}"
        except Exception as e:
            return False, f"Encryption failed: {e}"

    def decrypt_file(self, input_path: str, output_path: str) -> Tuple[bool, str]:
        """
        Decrypt a file and save to output path.

        Args:
            input_path: Path to encrypted file
            output_path: Path to save decrypted file

        Returns:
            Tuple of (success, message)
        """
        try:
            with open(input_path, 'rb') as f:
                encrypted = f.read()

            decrypted = self.decrypt_data(encrypted)

            with open(output_path, 'wb') as f:
                f.write(decrypted)

            return True, f"Decrypted {len(encrypted)} bytes -> {len(decrypted)} bytes"

        except FileNotFoundError:
            return False, f"Input file not found: {input_path}"
        except Exception as e:
            return False, f"Decryption failed: {e}"

    @staticmethod
    def pad_data(data: bytes) -> bytes:
        """
        Pad data to 16-byte boundary without encryption.
        Useful for preparing unencrypted firmware for ESP32.

        Args:
            data: Raw binary data

        Returns:
            Padded data
        """
        if not load_crypto():
            # Manual PKCS7 padding if crypto not available
            padding_len = 16 - (len(data) % 16)
            return data + bytes([padding_len] * padding_len)

        return pad(data, 16)

    @staticmethod
    def get_padded_size(original_size: int) -> int:
        """
        Calculate size after PKCS7 padding.

        Args:
            original_size: Original data size

        Returns:
            Size after padding
        """
        padding_len = 16 - (original_size % 16)
        return original_size + padding_len


def validate_key_iv(key: str, iv: str) -> Tuple[bool, str]:
    """
    Validate AES key and IV.

    Args:
        key: AES key to validate
        iv: IV to validate

    Returns:
        Tuple of (is_valid, error_message)
    """
    if not key:
        return False, "Key is required"
    if not iv:
        return False, "IV is required"
    if len(key) != 16:
        return False, f"Key must be 16 characters (got {len(key)})"
    if len(iv) != 16:
        return False, f"IV must be 16 characters (got {len(iv)})"

    # Note: weak key check removed - user is responsible for key security
    return True, "Key and IV are valid"


# Quick test
if __name__ == "__main__":
    if not load_crypto():
        print("PyCryptodome not installed. Run: pip install pycryptodome")
    else:
        print("Testing encryption...")

        # Test encrypt/decrypt
        enc = FWEncryptor("MySecretKey12345", "InitVector123456")

        original = b"Hello, ESP32 Firmware World!"
        encrypted = enc.encrypt_data(original)
        decrypted = enc.decrypt_data(encrypted)

        print(f"Original:  {original}")
        print(f"Encrypted: {encrypted.hex()[:50]}...")
        print(f"Decrypted: {decrypted}")
        print(f"Match: {original == decrypted}")
