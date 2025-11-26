import os
try:
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import pad
    HAS_CRYPTO = True
except ImportError:
    HAS_CRYPTO = False

class FWEncryptor:
    def __init__(self, key_str: str, iv_str: str):
        if not HAS_CRYPTO: raise ImportError("Chưa cài thư viện pycryptodome")
        
        # Chuyển đổi string sang bytes
        self.key = key_str.encode('utf-8') if isinstance(key_str, str) else key_str
        self.iv = iv_str.encode('utf-8') if isinstance(iv_str, str) else iv_str

        # Validate độ dài
        if len(self.key) not in [16, 24, 32]:
            raise ValueError(f"Key phải dài 16, 24 hoặc 32 bytes. Hiện tại: {len(self.key)}")
        if len(self.iv) != 16:
            raise ValueError(f"IV phải dài đúng 16 bytes. Hiện tại: {len(self.iv)}")

    # --- [MỚI] Hàm này dùng để mã hóa dữ liệu thô (bytes) ---
    def encrypt_data(self, raw_data: bytes) -> bytes:
        # Init Cipher AES-CBC
        cipher = AES.new(self.key, AES.MODE_CBC, self.iv)
        # Padding & Encrypt
        return cipher.encrypt(pad(raw_data, AES.block_size))

    # Hàm cũ (giữ lại nếu cần dùng cho code cũ)
    def encrypt_file(self, input_path: str, output_path: str = None) -> bool:
        if not os.path.exists(input_path): return False
        if output_path is None:
            filename, _ = os.path.splitext(input_path)
            output_path = filename + ".enc"

        try:
            with open(input_path, 'rb') as f: raw_data = f.read()
            # Gọi lại hàm encrypt_data cho gọn
            encrypted_data = self.encrypt_data(raw_data)
            with open(output_path, 'wb') as f: f.write(encrypted_data)
            return True
        except Exception as e:
            print(f"Error: {e}")
            return False