#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Firmware Library Manager (Tkinter) - v2.1 (Fixed filenames)
- Quản lý firmware theo metadata JSON (app, bootloader, partition).
- Thư viện cục bộ: firmware_library/<fw_id>/[FW.bin, BOTL.bin, PART.bin, fw_metadata.json]
- Thẻ nhớ:
  - <sd_root>/index.txt (file index tổng)
  - <sd_root>/<fw_id>/[FW.bin, BOTL.bin, PART.bin]
"""
import os
import shutil
import json
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
import hashlib
import subprocess # Cần cho 'on_open_folder'

# Lazy load: Crypto và esp_encryptor sẽ được import khi cần
# Giúp khởi động app nhanh hơn
_crypto_loaded = False
AES = None
pad = None
FWEncryptor = None

def _load_crypto():
    """Lazy load Crypto libraries khi cần."""
    global _crypto_loaded, AES, pad, FWEncryptor
    if _crypto_loaded:
        return True
    try:
        from Crypto.Cipher import AES as _AES
        from Crypto.Util.Padding import pad as _pad
        AES = _AES
        pad = _pad
        _crypto_loaded = True
    except ImportError:
        return False
    # Load encryptor
    try:
        from esp_encryptor import FWEncryptor as _FWEnc
        FWEncryptor = _FWEnc
    except ImportError:
        pass
    return True

# ---------------- Utility Functions ----------------
APP_TITLE = "Firmware Library Manager (Metadata Index)"
# Lấy thư mục chứa exe (hoặc script khi dev)
if getattr(sys, 'frozen', False):
    APP_DIR = os.path.dirname(sys.executable)
else:
    APP_DIR = os.path.dirname(os.path.abspath(__file__))
LIB_ROOT = os.path.join(APP_DIR, "firmware_library")
SD_INDEX_FILE = "index.txt" # File index trên thẻ nhớ
LOCAL_META_FILE = "fw_metadata.json" # File metadata trong thư viện
CONFIG_FILE = os.path.join(APP_DIR, "tool_setting.json")

# Đảm bảo thư mục thư viện tồn tại
def ensure_lib_root():
    os.makedirs(LIB_ROOT, exist_ok=True)

def safe_name(name: str) -> str:
    # sanitize folder name (cho phép chữ hoa)
    s = name.strip().replace(" ", "_")
    allowed = "-_.0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    return "".join(c for c in s if c in allowed)

def generate_short_fw_id():
    """Sinh fw_id ngắn dạng FW001, FW002... (FAT32 5.3 compatible - max 5 ký tự)."""
    ensure_lib_root()
    existing = set()
    for name in os.listdir(LIB_ROOT):
        if name.upper().startswith("FW") and len(name) <= 5:
            existing.add(name.upper())

    # Tìm số tiếp theo (FW001 -> FW999)
    for i in range(1, 1000):
        fw_id = f"FW{i:03d}"  # FW001, FW002, ...
        if fw_id not in existing:
            return fw_id
    return f"FW{int(time.time()) % 1000:03d}"  # Fallback

def shorten_device_type(name: str) -> str:
    """Rút gọn device_type: GAP_THU_DIEN_DUNG -> GTDD (viết tắt chữ cái đầu)."""
    if not name:
        return ""
    # Tách theo _ và lấy chữ cái đầu mỗi từ
    parts = name.replace("-", "_").split("_")
    abbrev = "".join(p[0].upper() for p in parts if p)
    return abbrev[:8]  # Max 8 ký tự

def shorten_version(ver: str) -> str:
    """Rút gọn version: v1.1.2_XXX_H -> v1.1_H (giữ vX.Y + chữ cái cuối V/H)."""
    if not ver:
        return ""
    import re

    # Lấy chữ cái cuối nếu là V hoặc H (loại thiết bị)
    suffix = ""
    if ver and ver[-1].upper() in ('V', 'H'):
        suffix = "_" + ver[-1].upper()

    # Tìm pattern vX.Y hoặc vX.Y.Z
    match = re.match(r'(v?\d+\.\d+)', ver, re.IGNORECASE)
    if match:
        result = match.group(1)
        if not result.lower().startswith('v'):
            result = 'v' + result
        return result + suffix
    return ver[:5] + suffix  # Fallback

def calculate_md5(filepath):
    """Tính toán MD5 của một file."""
    hash_md5 = hashlib.md5()
    try:
        with open(filepath, "rb") as f:
            for chunk in iter(lambda: f.read(4096), b""):
                hash_md5.update(chunk)
        return hash_md5.hexdigest()
    except Exception as e:
        print(f"Error calculating MD5 for {filepath}: {e}")
        return None

def list_firmwares():
    """List các firmware trong thư viện cục bộ (phải có fw_metadata.json)."""
    ensure_lib_root()
    items = []
    for name in os.listdir(LIB_ROOT):
        path = os.path.join(LIB_ROOT, name)
        meta_path = os.path.join(path, LOCAL_META_FILE)
        if os.path.isdir(path) and os.path.isfile(meta_path):
            try:
                with open(meta_path, "r", encoding="utf-8") as f:
                    meta = json.load(f)
                items.append((name, path, meta))
            except Exception as e:
                print(f"Could not load metadata for {name}: {e}")
    items.sort()
    return items

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("950x600")
        ensure_lib_root()

        # Biến cho tab Add Firmware
        self.add_fw_id_var = tk.StringVar()
        self.add_fw_id_var.trace("w", self.on_fw_id_change)
        self.add_device_type_var = tk.StringVar()
        self.add_version_var = tk.StringVar()

        # Biến lưu trữ đường dẫn file cho tab "Add"
        self.add_app_path = tk.StringVar()
        self.add_boot_path = tk.StringVar()
        self.add_part_path = tk.StringVar()
        # --- 1. Nạp cấu hình cũ ---
        saved_cfg = self.load_settings()

        # --- 2. Khởi tạo biến với giá trị đã lưu (nếu có) ---
        # Nếu không có file save thì dùng chuỗi mặc định "123..."
        self.enc_key = tk.StringVar(value=saved_cfg.get("key", "1234567890123456"))
        self.enc_iv = tk.StringVar(value=saved_cfg.get("iv", "0000000000000000"))

        # Lưu luôn cái đường dẫn SD cũ, đỡ phải chọn lại mỗi lần mở
        self.sd_path_var = tk.StringVar(value=saved_cfg.get("sd_path", ""))
        self.git_repo_url = tk.StringVar(value=saved_cfg.get("repo_url", ""))
        # Biến lưu trữ index của thẻ SD
        self.sd_index_data = [] # List of metadata objects

        # --- Cloud Sync Variables ---
        self.cloud_base_url = tk.StringVar(value=saved_cfg.get("cloud_url", "http://firmware.quanlytuxa.com"))
        self.cloud_username = tk.StringVar(value=saved_cfg.get("cloud_user", ""))
        self.cloud_password = tk.StringVar(value=saved_cfg.get("cloud_pass", ""))
        self.cloud_token = None  # JWT token sau khi login
        self.cloud_customers = []  # List customers từ API
        self.cloud_firmware_list = []  # List firmware versions

       # --- SETUP STYLE (GIAO DIỆN ĐẸP) ---
        style = ttk.Style()
        style.theme_use('clam') # Dùng theme 'clam' hoặc 'alt' để nút bấm đẹp hơn mặc định

        # Style cho Tiêu đề đậm
        style.configure("Bold.TLabel", font=("Segoe UI", 10, "bold"))
        # Style cho Nút thường
        style.configure("TButton", font=("Segoe UI", 9), padding=5)
        # Style cho Nút Hành Động Chính (Nổi bật)
        style.configure("Primary.TButton", font=("Segoe UI", 10, "bold"), foreground="blue")
        # Style cho Nút Cảnh báo (Xóa)
        style.configure("Danger.TButton", font=("Segoe UI", 9), foreground="red")

        self._build_ui()
        self.refresh_firmware_list()

    def _build_ui(self):
        nb = ttk.Notebook(self)
        nb.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)

        # Tab: Add firmware
        frm_add = ttk.Frame(nb)
        nb.add(frm_add, text="Thêm firmware (mới)")
        self._build_add_tab(frm_add)

        # Tab: Manage library & SD Card
        frm_manage = ttk.Frame(nb)
        nb.add(frm_manage, text="Quản lý & Đồng bộ SD")
        self._build_manage_tab(frm_manage)

        # Tab: Sync Cloud (NEW)
        frm_cloud = ttk.Frame(nb)
        nb.add(frm_cloud, text="☁️ Sync Cloud")
        self._build_cloud_tab(frm_cloud)

        # Log area (bottom)
        log_frame = ttk.Frame(self)
        log_frame.pack(fill=tk.BOTH, expand=False, padx=6, pady=(0,6))
        ttk.Label(log_frame, text="Log:").pack(anchor="w")
        self.log = scrolledtext.ScrolledText(log_frame, height=8)
        self.log.pack(fill=tk.BOTH, expand=True)

    # ---------------- Add tab (Refactored & Beautified) ----------------
    # ---------------- Add tab (Nâng cấp Dropdown Gợi ý) ----------------
    def _build_add_tab(self, parent):
        frm = ttk.Frame(parent, padding=20)
        frm.pack(fill=tk.BOTH, expand=True)

        # Lấy dữ liệu gợi ý ban đầu
        ids_hint, types_hint = self.get_library_suggestions()

        # ==================================================
        # GROUP 1: THÔNG TIN CƠ BẢN
        # ==================================================
        grp_info = ttk.LabelFrame(frm, text="1. Thông tin Firmware", padding=15)
        grp_info.pack(fill=tk.X, pady=(0, 15))

        grp_info.columnconfigure(1, weight=1)

        # --- ROW 0: FIRMWARE ID (Combobox) ---
        ttk.Label(grp_info, text="Firmware ID (Tên Folder):", style="Bold.TLabel").grid(row=0, column=0, sticky="w", pady=5)

        if not hasattr(self, 'add_fw_id_var'):
            self.add_fw_id_var = tk.StringVar()
            self.add_fw_id_var.trace("w", self.on_fw_id_change)

        # Dùng Combobox thay vì Entry
        self.combo_id = ttk.Combobox(grp_info, textvariable=self.add_fw_id_var, values=ids_hint, font=("Consolas", 10, "bold"))
        self.combo_id.grid(row=0, column=1, padx=10, sticky="ew")
        self.combo_id.bind("<<ComboboxSelected>>", self.on_id_selected) # Bắt sự kiện chọn

        self.add_fw_id_entry = self.combo_id # Để hàm sanitize dùng lại

        # --- ROW 1: DEVICE TYPE (Combobox) ---
        ttk.Label(grp_info, text="Device Type (Gợi ý):").grid(row=1, column=0, sticky="w", pady=5)

        # Dùng Combobox để gợi ý các loại chip đã dùng trước đó
        self.combo_type = ttk.Combobox(grp_info, textvariable=self.add_device_type_var, values=types_hint, font=("Consolas", 10))
        self.combo_type.grid(row=1, column=1, padx=10, sticky="ew")

        # --- ROW 2: VERSION ---
        ttk.Label(grp_info, text="Version (vd: 1.0.0):").grid(row=2, column=0, sticky="w", pady=5)
        ttk.Entry(grp_info, textvariable=self.add_version_var, font=("Consolas", 10)).grid(row=2, column=1, padx=10, sticky="ew")


        # ==================================================
        # GROUP 2: FILE NGUỒN
        # ==================================================
        grp_files = ttk.LabelFrame(frm, text="2. File Nguồn (.bin)", padding=15)
        grp_files.pack(fill=tk.X, pady=(0, 20))

        def draw_file_row(row_idx, label_text, var, cmd):
            ttk.Label(grp_files, text=label_text).grid(row=row_idx, column=0, sticky="w", pady=5)
            e = ttk.Entry(grp_files, textvariable=var, state="readonly", font=("Segoe UI", 9))
            e.grid(row=row_idx, column=1, padx=10, sticky="ew")
            ttk.Button(grp_files, text="📂 Chọn...", width=8, command=cmd).grid(row=row_idx, column=2, sticky="e")

        grp_files.columnconfigure(1, weight=1)
        draw_file_row(0, "Application (-> FW.bin):",   self.add_app_path,  self.add_choose_app)
        draw_file_row(1, "Bootloader (-> BOTL.bin):",  self.add_boot_path, self.add_choose_boot)
        draw_file_row(2, "Partition (-> PART.bin):",   self.add_part_path, self.add_choose_part)

        # ==================================================
        # GROUP 3: ACTIONS
        # ==================================================
        grp_act = ttk.Frame(frm)
        grp_act.pack(fill=tk.X, pady=10)

        ttk.Button(grp_act, text="🧹 Làm mới Form", style="Danger.TButton", command=self.add_clear).pack(side=tk.LEFT)

        # Lưu biến nút bấm để đổi tên động
        self.btn_add = ttk.Button(grp_act, text="➕ THÊM MỚI FIRMWARE", style="Primary.TButton", width=30, command=self.add_commit)
        self.btn_add.pack(side=tk.RIGHT)

    def on_fw_id_change(self, *args):
        """Tự động sanitize tên folder khi gõ."""
        raw_name = self.add_fw_id_var.get()
        sanitized = safe_name(raw_name)
        if raw_name != sanitized:
            self.add_fw_id_entry.delete(0, tk.END)
            self.add_fw_id_entry.insert(0, sanitized)

    def add_choose_app(self):
        p = filedialog.askopenfilename(title="Chọn file Application (.bin)")
        if p: self.add_app_path.set(p)

    def add_choose_boot(self):
        p = filedialog.askopenfilename(title="Chọn file Bootloader (.bin)")
        if p: self.add_boot_path.set(p)

    def add_choose_part(self):
        p = filedialog.askopenfilename(title="Chọn file Partition (.bin)")
        if p: self.add_part_path.set(p)

    def add_clear(self):
        self.add_fw_id_var.set("")
        self.add_device_type_var.set("")
        self.add_version_var.set("")
        self.add_app_path.set("")
        self.add_boot_path.set("")
        self.add_part_path.set("")

    def add_commit(self):
        fw_id = self.add_fw_id_var.get()
        dev_type = self.add_device_type_var.get().strip()
        version = self.add_version_var.get().strip()

        app_path = self.add_app_path.get()
        boot_path = self.add_boot_path.get()
        part_path = self.add_part_path.get()

        if not all([fw_id, dev_type, version]):
            messagebox.showerror("Thiếu thông tin", "Vui lòng nhập fw_id, device_type và version.")
            return
        if not all([app_path, boot_path, part_path]):
            messagebox.showerror("Thiếu file", "Vui lòng chọn đủ 3 file: app, bootloader và partition.")
            return

        folder_name = fw_id # đã được sanitize
        dest = os.path.join(LIB_ROOT, folder_name)

        # Kiểm tra file nguồn có nằm trong folder đích không
        def is_inside_dest(file_path):
            try:
                return os.path.commonpath([os.path.abspath(file_path), os.path.abspath(dest)]) == os.path.abspath(dest)
            except ValueError:
                return False

        source_in_dest = any(is_inside_dest(p) for p in [app_path, boot_path, part_path] if p)

        # Nếu file nguồn nằm trong folder đích, đọc vào memory trước
        file_contents = {}
        if source_in_dest and os.path.exists(dest):
            if not messagebox.askyesno("Cập nhật", f"Firmware '{folder_name}' đã tồn tại. Cập nhật lại?"):
                return
            # Đọc file vào memory trước khi xóa
            for name, path in [("app", app_path), ("boot", boot_path), ("part", part_path)]:
                if path and os.path.exists(path):
                    with open(path, "rb") as f:
                        file_contents[name] = f.read()
            shutil.rmtree(dest)
        elif os.path.exists(dest):
            if not messagebox.askyesno("Trùng tên", f"Firmware '{folder_name}' đã tồn tại. Ghi đè?"):
                return
            shutil.rmtree(dest)

        os.makedirs(dest, exist_ok=True)

        try:
            # === THAY ĐỔI: Đặt tên file cố định ===
            dest_app = os.path.join(dest, "FW.bin")
            dest_boot = os.path.join(dest, "BOTL.bin")
            dest_part = os.path.join(dest, "PART.bin")

            # Copy files (và đổi tên) - từ memory nếu có, hoặc từ file gốc
            if file_contents:
                with open(dest_app, "wb") as f:
                    f.write(file_contents["app"])
                with open(dest_boot, "wb") as f:
                    f.write(file_contents["boot"])
                with open(dest_part, "wb") as f:
                    f.write(file_contents["part"])
            else:
                shutil.copy2(app_path, dest_app)
                shutil.copy2(boot_path, dest_boot)
                shutil.copy2(part_path, dest_part)

            # Tính MD5 (tính trên file đã copy trong dest)
            md5_app = calculate_md5(dest_app)
            md5_boot = calculate_md5(dest_boot)
            md5_part = calculate_md5(dest_part)

            if not all([md5_app, md5_boot, md5_part]):
                 raise Exception("Không thể tính toán MD5. Kiểm tra file.")

            # === THAY ĐỔI: Cập nhật metadata trỏ đến tên file mới ===
            metadata = {
                "fw_id": fw_id,
                "device_type": dev_type,
                "version": version,
                "path": f"{folder_name}/FW.bin",
                "md5": md5_app,
                "path_bootloader": f"{folder_name}/BOTL.bin",
                "md5_bootloader": md5_boot,
                "path_partition": f"{folder_name}/PART.bin",
                "md5_partition": md5_part
            }

            # Lưu metadata vào thư mục thư viện (fw_metadata.json)
            meta_path = os.path.join(dest, LOCAL_META_FILE)
            with open(meta_path, "w", encoding="utf-8") as f:
                json.dump(metadata, f, indent=2, ensure_ascii=False)

            self.log_msg(f"Đã thêm firmware '{folder_name}' vào thư viện (với tên file FW.bin, BOTL.bin, PART.bin).")
            self.add_clear()
            self.refresh_firmware_list()

        except Exception as e:
            messagebox.showerror("Lỗi copy", f"Lỗi khi thêm firmware: {e}")
            # Xoá thư mục nếu tạo lỗi
            if os.path.isdir(dest):
                shutil.rmtree(dest)

    # ---------------- Manage tab (Refactored) ----------------
    def _build_manage_tab(self, parent):
        frm = ttk.Frame(parent, padding=10)
        frm.pack(fill=tk.BOTH, expand=True)

        # ==================================================
        # CỘT 1: LOCAL LIBRARY
        # ==================================================
        left = ttk.LabelFrame(frm, text="1. Thư viện Cục bộ (Local)", padding=5)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=5)

        self.fw_listbox = tk.Listbox(left, width=30, selectmode=tk.EXTENDED, font=("Consolas", 10))
        self.fw_listbox.pack(fill=tk.BOTH, expand=True, pady=5)
        self.fw_listbox.bind("<<ListboxSelect>>", self.on_fw_select)

        btn_l = ttk.Frame(left)
        btn_l.pack(fill=tk.X)
        ttk.Button(btn_l, text="❌ Xoá FW", style="Danger.TButton", command=self.on_delete_fw).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2)
        ttk.Button(btn_l, text="🔄 Refresh", command=self.refresh_firmware_list).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2)

        # ==================================================
        # CỘT 2: CENTER CONTROL (Trung tâm điều khiển)
        # ==================================================
        mid = ttk.Frame(frm)
        mid.pack(side=tk.LEFT, fill=tk.Y, padx=10)

        # --- PHẦN A: CẤU HÌNH (Inputs) ---
        grp_cfg = ttk.LabelFrame(mid, text="Cấu hình Kết nối & Mã hóa", padding=10)
        grp_cfg.pack(fill=tk.X, pady=(0, 10))

        # Key & IV
        ttk.Label(grp_cfg, text="AES Key (16 chars):").pack(anchor="w")
        ttk.Entry(grp_cfg, textvariable=self.enc_key, font=("Consolas", 10)).pack(fill=tk.X, pady=(0, 5))

        ttk.Label(grp_cfg, text="AES IV (16 chars):").pack(anchor="w")
        ttk.Entry(grp_cfg, textvariable=self.enc_iv, font=("Consolas", 10)).pack(fill=tk.X, pady=(0, 5))

        ttk.Separator(grp_cfg, orient='horizontal').pack(fill='x', pady=10)

        # Git URL
        ttk.Label(grp_cfg, text="Git Repository URL (.git):").pack(anchor="w")
        ttk.Entry(grp_cfg, textvariable=self.git_repo_url, font=("Consolas", 10)).pack(fill=tk.X, pady=(0, 5))

        # Nút Lưu cấu hình nhỏ gọn nằm ngay dưới
        ttk.Button(grp_cfg, text="💾 Lưu Cấu Hình", command=self.save_settings).pack(fill=tk.X, pady=5)


        # --- PHẦN B: ACTION CENTER (Các nút to) ---
        grp_act = ttk.LabelFrame(mid, text="Thao tác (Actions)", padding=10)
        grp_act.pack(fill=tk.BOTH, expand=True)

        BTN_WIDTH = 35 # Độ rộng chuẩn cho tất cả nút

        # Nút Copy Offline (Plain - không mã hóa)
        ttk.Button(grp_act, text="📂 Copy sang Thẻ Nhớ (Plain)", width=BTN_WIDTH,
                   command=self.on_copy_to_sd).pack(pady=5)

        # Nút Copy Encrypted (MỚI - giữ file mã hóa trên SD)
        ttk.Button(grp_act, text="🔐 Copy Encrypted sang SD", width=BTN_WIDTH,
                   command=self.on_copy_encrypted_to_sd).pack(pady=5)

        # Nút Export Local
        ttk.Button(grp_act, text="📦 Mã hóa & Xuất file (Local)", width=BTN_WIDTH,
                   command=self.on_export_git).pack(pady=5)

        # Nút Xóa khỏi SD (Gom vào đây luôn cho tiện tay)
        ttk.Button(grp_act, text="🗑️ Xoá khỏi Thẻ Nhớ", width=BTN_WIDTH, style="Danger.TButton",
                   command=self.on_remove_from_sd).pack(pady=5)

        ttk.Separator(grp_act, orient='horizontal').pack(fill='x', pady=15)

        # Nút PUSH TO CLOUD (Nổi bật nhất)
        ttk.Button(grp_act, text="☁️ PUSH LÊN CLOUD NGAY", width=BTN_WIDTH, style="Primary.TButton",
                   command=self.on_push_to_git).pack(pady=5, ipady=5) # ipady làm nút cao hơn tí


        # ==================================================
        # CỘT 3: SD CARD & INFO
        # ==================================================
        right = ttk.Frame(frm)
        right.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)

        # SD Card Section
        grp_sd = ttk.LabelFrame(right, text="2. Thẻ nhớ (SD Card)", padding=5)
        grp_sd.pack(fill=tk.BOTH, expand=True, pady=(0, 10))

        row_sd = ttk.Frame(grp_sd)
        row_sd.pack(fill=tk.X)
        ttk.Entry(row_sd, textvariable=self.sd_path_var).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0,5))
        ttk.Button(row_sd, text="Chọn...", width=8, command=self.choose_sd_dir).pack(side=tk.LEFT)

        ttk.Button(grp_sd, text="Tải danh sách thẻ", command=self.load_sd_content).pack(fill=tk.X, pady=5)

        self.sd_listbox = tk.Listbox(grp_sd, height=10)
        self.sd_listbox.pack(fill=tk.BOTH, expand=True)
        self.sd_listbox.bind("<<ListboxSelect>>", self.on_sd_select)

        # Metadata Details Section
        grp_meta = ttk.LabelFrame(right, text="Chi tiết (Metadata)", padding=5)
        grp_meta.pack(fill=tk.BOTH, expand=True)

        self.fw_detail = scrolledtext.ScrolledText(grp_meta, height=10, font=("Consolas", 9))
        self.fw_detail.pack(fill=tk.BOTH, expand=True)

    def choose_sd_dir(self):
        d = filedialog.askdirectory(title="Chọn đường dẫn mount của thẻ nhớ")
        if d:
            self.sd_path_var.set(d)
            self.load_sd_content() # Tự động tải

    def load_sd_content(self):
        """Đọc file index.txt từ thẻ nhớ."""
        self.sd_listbox.delete(0, tk.END)
        self.sd_index_data = []
        root = self.sd_path_var.get().strip()
        if not os.path.isdir(root):
            messagebox.showerror("Sai đường dẫn", "Thẻ nhớ không hợp lệ hoặc chưa mount.")
            return

        index_path = os.path.join(root, SD_INDEX_FILE)

        if not os.path.isfile(index_path):
            self.log_msg(f"Không tìm thấy file '{SD_INDEX_FILE}'. Thẻ có thể trống.")
            return # Không có file index, coi như trống

        try:
            with open(index_path, "r", encoding="utf-8") as f:
                self.sd_index_data = json.load(f)

            if not isinstance(self.sd_index_data, list):
                raise Exception("File index không phải là một JSON list (array).")

            for item in self.sd_index_data:
                fw_id = item.get("fw_id", "unknown_id")
                self.sd_listbox.insert(tk.END, fw_id)
            self.log_msg(f"Đã tải {len(self.sd_index_data)} mục từ '{SD_INDEX_FILE}'.")

        except Exception as e:
            messagebox.showerror("Lỗi đọc index", f"Không thể đọc file {SD_INDEX_FILE}: {e}")
            self.sd_index_data = []

    def save_sd_index(self):
        """Lưu self.sd_index_data (đã cập nhật) ra file index.txt trên thẻ."""
        root = self.sd_path_var.get().strip()
        if not os.path.isdir(root):
            messagebox.showerror("Sai đường dẫn", "Không tìm thấy đường dẫn thẻ nhớ để lưu.")
            return False

        index_path = os.path.join(root, SD_INDEX_FILE)
        try:
            with open(index_path, "w", encoding="utf-8") as f:
                json.dump(self.sd_index_data, f, indent=2, ensure_ascii=False)
            self.log_msg(f"Đã cập nhật file '{SD_INDEX_FILE}' trên thẻ.")
            return True
        except Exception as e:
            messagebox.showerror("Lỗi lưu index", f"Không thể lưu {SD_INDEX_FILE}: {e}")
            return False

    def on_sd_select(self, evt=None):
        """Hiển thị metadata khi chọn firmware trên thẻ SD."""
        sel = self.sd_listbox.curselection()
        if not sel: return

        idx = sel[0]
        try:
            meta = self.sd_index_data[idx]
            self.fw_detail.delete("1.0", tk.END)
            self.fw_detail.insert(tk.END, f"Metadata từ thẻ (index.txt):\n\n")
            self.fw_detail.insert(tk.END, json.dumps(meta, indent=2, ensure_ascii=False))
        except IndexError:
            self.fw_detail.delete("1.0", tk.END)
            self.fw_detail.insert(tk.END, "Lỗi: Không tìm thấy data tương ứng.")

    def refresh_firmware_list(self):
        """Tải lại danh sách firmware từ thư viện cục bộ."""
        items = list_firmwares()
        self.fw_listbox.delete(0, tk.END)
        for name, path, meta in items:
            self.fw_listbox.insert(tk.END, name)

        # (MỚI) Cập nhật lại dữ liệu gợi ý cho Tab Add
        # Kiểm tra xem UI đã được vẽ chưa để tránh lỗi khởi tạo
        if hasattr(self, 'combo_id'):
            ids, types = self.get_library_suggestions()
            self.combo_id['values'] = ids
            self.combo_type['values'] = types

        if hasattr(self, 'fw_detail'):
            self.fw_detail.delete("1.0", tk.END)

    def on_fw_select(self, evt=None):
        """Hiển thị metadata khi chọn firmware trong thư viện cục bộ."""
        sel = self.fw_listbox.curselection()
        if not sel: return

        name = self.fw_listbox.get(sel[0])
        path = os.path.join(LIB_ROOT, name)
        meta_path = os.path.join(path, LOCAL_META_FILE)

        self.fw_detail.delete("1.0", tk.END)
        self.fw_detail.insert(tk.END, f"Name: {name}\nPath: {path}\n\n")

        if os.path.isfile(meta_path):
            try:
                with open(meta_path, "r", encoding="utf-8") as fh:
                    data = json.load(fh)
                self.fw_detail.insert(tk.END, "Metadata (cục bộ):\n")
                self.fw_detail.insert(tk.END, json.dumps(data, indent=2, ensure_ascii=False))
            except Exception as e:
                self.fw_detail.insert(tk.END, f"\nKhông đọc được metadata: {e}")
        else:
            self.fw_detail.insert(tk.END, "Không tìm thấy file fw_metadata.json!")

        # Hiển thị các file thực tế trong thư mục
        self.fw_detail.insert(tk.END, "\n\nFiles (thực tế trong thư mục):\n")
        if os.path.isdir(path):
            for f in os.listdir(path):
                self.fw_detail.insert(tk.END, f" - {f}\n")


    def on_delete_fw(self):
        selections = self.fw_listbox.curselection()
        if not selections: return

        count = len(selections)
        if not messagebox.askyesno("Xác nhận", f"Bạn có chắc muốn xoá {count} firmware này?\n(Sẽ xóa Local, Git Staging và cập nhật Index)"):
            return

        git_release_root = os.path.join(os.getcwd(), "_release_for_git")

        # 1. VÒNG LẶP XÓA
        for idx in reversed(selections):
            name = self.fw_listbox.get(idx)
            try:
                # Xóa Local
                local_path = os.path.join(LIB_ROOT, name)
                if os.path.exists(local_path): shutil.rmtree(local_path)

                # Xóa Git Release (Folder chứa .enc)
                git_path = os.path.join(git_release_root, name)
                if os.path.exists(git_path): shutil.rmtree(git_path)

                self.log_msg(f"Đã xóa: {name}")
            except Exception as e:
                self.log_msg(f"Lỗi xóa {name}: {e}")

        self.refresh_firmware_list()

        # 2. CẬP NHẬT LẠI FILE INDEX.TXT CHO GIT (Quan trọng!)
        # Quét lại những gì còn sót lại trong thư viện để tạo index mới
        if os.path.exists(git_release_root):
            new_git_index = []

            # Duyệt tất cả các folder còn sống trong thư viện
            for fw_name in os.listdir(LIB_ROOT):
                src_dir = os.path.join(LIB_ROOT, fw_name)
                meta_path = os.path.join(src_dir, LOCAL_META_FILE)

                # Chỉ thêm vào index nếu có file metadata hợp lệ
                if os.path.isdir(src_dir) and os.path.isfile(meta_path):
                    try:
                        with open(meta_path, "r", encoding="utf-8") as f:
                            meta = json.load(f)
                        new_git_index.append(meta)
                    except: pass

            # Ghi đè file index.txt cũ
            index_path = os.path.join(git_release_root, "index.txt")
            with open(index_path, "w", encoding="utf-8") as f:
                json.dump(new_git_index, f, indent=2, ensure_ascii=False)

            self.log_msg("Đã cập nhật lại index.txt cho Git (loại bỏ firmware đã xóa).")

        messagebox.showinfo("Lưu ý", "Đã xóa xong.\nHãy bấm nút 'PUSH LÊN CLOUD' để đồng bộ việc xóa này lên Server!")
    def on_open_folder(self):
        sel = self.fw_listbox.curselection()
        if not sel:
            messagebox.showinfo("Chọn firmware", "Vui lòng chọn firmware để mở thư mục.")
            return

        name = self.fw_listbox.get(sel[0])
        path = os.path.join(LIB_ROOT, name)
        try:
            if os.name == "nt": # Windows
                os.startfile(path)
            elif os.uname().sysname == "Darwin": # macOS
                subprocess.run(["open", path])
            else: # Linux
                subprocess.run(["xdg-open", path])
        except Exception as e:
            messagebox.showinfo("Folder", f"Lỗi mở thư mục: {e}\nĐường dẫn: {path}")

    def on_copy_to_sd(self):
        # 1. Kiểm tra đầu vào
        selections = self.fw_listbox.curselection()
        if not selections:
            return messagebox.showinfo("Info", "Vui lòng chọn firmware cần copy.")

        sd_root = self.sd_path_var.get().strip()
        if not os.path.isdir(sd_root):
            return messagebox.showerror("Lỗi", "Đường dẫn SD sai.")

        if not messagebox.askyesno("Xác nhận", f"Bạn muốn copy {len(selections)} firmware?\n(Sẽ tính toán và lưu thêm MD5 Padding vào metadata)"):
            return

        # Load thư viện Crypto (lazy load)
        if not _load_crypto():
            return messagebox.showerror("Lỗi", "Thiếu thư viện Crypto.")

        success_count = 0

        # 2. VÒNG LẶP XỬ LÝ
        for idx in selections:
            fw_name = self.fw_listbox.get(idx)
            src_dir = os.path.join(LIB_ROOT, fw_name)
            dest_dir = os.path.join(sd_root, fw_name)

            self.log_msg(f"--- Processing [{success_count + 1}/{len(selections)}]: {fw_name} ---")

            # Load metadata gốc (để đọc và cập nhật)
            local_meta_path = os.path.join(src_dir, LOCAL_META_FILE)
            try:
                with open(local_meta_path, "r", encoding="utf-8") as f:
                    local_meta = json.load(f)

                # Tạo bản copy để đưa xuống thẻ nhớ (Bản này sẽ sửa MD5 thành padded)
                sd_meta = local_meta.copy()
            except:
                self.log_msg(f"Skipped {fw_name}: Metadata lỗi.")
                continue

            # Xóa cũ tạo mới thư mục đích
            if os.path.exists(dest_dir): shutil.rmtree(dest_dir)
            os.makedirs(dest_dir, exist_ok=True)

            # Mapping file và tên trường MD5
            files_map = [
                ("FW.bin",   "path",            "md5"),
                ("BOTL.bin", "path_bootloader", "md5_bootloader"),
                ("PART.bin", "path_partition",  "md5_partition")
            ]

            has_change = False # Cờ kiểm tra xem có cần lưu lại file json local không

            for fname, key_path, key_md5 in files_map:
                src_file = os.path.join(src_dir, fname)
                dest_file = os.path.join(dest_dir, fname)

                if os.path.exists(src_file):
                    # --- PADDING & COPY ---
                    with open(src_file, "rb") as f_in: raw_data = f_in.read()

                    # 1. Tạo Padding
                    padded_data = pad(raw_data, AES.block_size)

                    # 2. Ghi xuống thẻ nhớ (File đã độn)
                    with open(dest_file, "wb") as f_out: f_out.write(padded_data)

                    # 3. Tính MD5 Padded
                    final_md5 = hashlib.md5(padded_data).hexdigest()

                    # 4. Cập nhật cho SD Index (Bắt buộc phải là MD5 Padded để ESP32 check)
                    sd_meta[key_path] = f"/{fw_name}/{fname}"
                    sd_meta[key_md5]  = final_md5

                    # 5. [QUAN TRỌNG] Cập nhật vào Local Metadata dưới dạng GHI CHÚ (Field mới)
                    # Tên trường ví dụ: "md5_padded", "md5_bootloader_padded"
                    key_padded = key_md5 + "_padded"
                    if local_meta.get(key_padded) != final_md5:
                        local_meta[key_padded] = final_md5
                        has_change = True

                    self.log_msg(f"  -> {fname}: OK (Padded)")
                else:
                    # Dọn dẹp thông tin nếu file thiếu
                    sd_meta[key_path] = ""
                    sd_meta[key_md5] = ""

            # --- LƯU NGƯỢC LẠI FILE LOCAL (NẾU CÓ TRƯỜNG MỚI) ---
            if has_change:
                try:
                    with open(local_meta_path, "w", encoding="utf-8") as f:
                        json.dump(local_meta, f, indent=2, ensure_ascii=False)
                    self.log_msg(f"  Note: Đã thêm info '_padded' vào file json gốc.")
                except Exception as e:
                    self.log_msg(f"  Warn: Không lưu được json gốc ({e})")

            # Cập nhật vào danh sách Index của thẻ nhớ
            self.sd_index_data = [i for i in self.sd_index_data if i.get("fw_id") != fw_name]
            self.sd_index_data.append(sd_meta)

            success_count += 1

        # 3. Lưu Index xuống thẻ nhớ
        if self.save_sd_index():
            self.load_sd_content()
            self.log_msg(f"===> DONE! Copy {success_count} mục.")
            messagebox.showinfo("Xong", f"Đã copy xong {success_count} mục.")

    def on_copy_encrypted_to_sd(self):
        """Copy firmware mã hóa (.enc) sang thẻ SD - ESP32 sẽ giải mã khi flash."""
        # 1. Kiểm tra đầu vào
        selections = self.fw_listbox.curselection()
        if not selections:
            return messagebox.showinfo("Info", "Vui lòng chọn firmware cần copy.")

        sd_root = self.sd_path_var.get().strip()
        if not os.path.isdir(sd_root):
            return messagebox.showerror("Lỗi", "Đường dẫn SD sai.")

        if not messagebox.askyesno("Xác nhận",
            f"Bạn muốn copy {len(selections)} firmware dưới dạng MÃ HÓA?\n"
            "(File .enc sẽ được giải mã bởi ESP32 khi flash)"):
            return

        # Load thư viện Crypto (lazy load)
        if not _load_crypto():
            return messagebox.showerror("Lỗi", "Thiếu thư viện Crypto.")
        if FWEncryptor is None:
            return messagebox.showerror("Lỗi", "Không tìm thấy esp_encryptor.py")

        # Validate Key/IV
        try:
            tool = FWEncryptor(self.enc_key.get(), self.enc_iv.get())
        except ValueError as e:
            return messagebox.showerror("Lỗi Key/IV", str(e))

        success_count = 0

        # 2. VÒNG LẶP XỬ LÝ
        for idx in selections:
            fw_name = self.fw_listbox.get(idx)
            src_dir = os.path.join(LIB_ROOT, fw_name)
            dest_dir = os.path.join(sd_root, fw_name)

            self.log_msg(f"--- Processing [{success_count + 1}/{len(selections)}]: {fw_name} (Encrypted) ---")

            # Load metadata gốc
            local_meta_path = os.path.join(src_dir, LOCAL_META_FILE)
            try:
                with open(local_meta_path, "r", encoding="utf-8") as f:
                    local_meta = json.load(f)
                sd_meta = local_meta.copy()
            except:
                self.log_msg(f"Skipped {fw_name}: Metadata lỗi.")
                continue

            # Xóa cũ tạo mới thư mục đích
            if os.path.exists(dest_dir):
                shutil.rmtree(dest_dir)
            os.makedirs(dest_dir, exist_ok=True)

            # Mapping file: tên gốc -> tên đích (.enc) -> trường path -> trường MD5
            files_map = [
                ("FW.bin",   "FW.enc",   "path",            "md5"),
                ("BOTL.bin", "BOTL.enc", "path_bootloader", "md5_bootloader"),
                ("PART.bin", "PART.enc", "path_partition",  "md5_partition")
            ]

            for fname_src, fname_dst, key_path, key_md5 in files_map:
                src_file = os.path.join(src_dir, fname_src)
                dest_file = os.path.join(dest_dir, fname_dst)

                if os.path.exists(src_file):
                    # --- MÃ HÓA & COPY ---
                    with open(src_file, "rb") as f_in:
                        raw_data = f_in.read()

                    # 1. Tạo Padding & Tính MD5 của dữ liệu PADDED (cho ESP32 verify)
                    padded_data = pad(raw_data, AES.block_size)
                    final_md5 = hashlib.md5(padded_data).hexdigest()

                    # 2. Mã hóa và ghi file .enc
                    enc_data = tool.encrypt_data(raw_data)
                    with open(dest_file, "wb") as f_out:
                        f_out.write(enc_data)

                    # 3. Cập nhật metadata cho SD
                    # Path: Trỏ đến file .enc
                    # MD5: MD5 của dữ liệu ĐÃ PADDING (sau khi giải mã sẽ khớp)
                    sd_meta[key_path] = f"/{fw_name}/{fname_dst}"
                    sd_meta[key_md5] = final_md5

                    self.log_msg(f"  -> {fname_dst}: OK (Encrypted)")
                else:
                    # Dọn dẹp nếu file thiếu
                    sd_meta[key_path] = ""
                    sd_meta[key_md5] = ""

            # [QUAN TRỌNG] Đánh dấu firmware này là ENCRYPTED
            sd_meta["encrypted"] = True

            # Cập nhật vào danh sách Index của thẻ nhớ
            self.sd_index_data = [i for i in self.sd_index_data if i.get("fw_id") != fw_name]
            self.sd_index_data.append(sd_meta)

            success_count += 1

        # 3. Lưu Index xuống thẻ nhớ
        if self.save_sd_index():
            self.load_sd_content()
            self.log_msg(f"===> DONE! Copy Encrypted {success_count} mục.")
            messagebox.showinfo("Xong",
                f"Đã copy xong {success_count} mục (Encrypted).\n"
                "ESP32 sẽ giải mã khi flash.")

    def on_remove_from_sd(self):
        """Xoá firmware khỏi thẻ SD và cập nhật index.txt."""
        sel = self.sd_listbox.curselection()
        if not sel:
            messagebox.showinfo("Chọn firmware", "Chọn firmware (bên phải, trên thẻ) để xoá.")
            return

        sd_root = self.sd_path_var.get().strip()
        if not os.path.isdir(sd_root):
            messagebox.showerror("Sai đường dẫn", "Đường dẫn thẻ SD không hợp lệ.")
            return

        try:
            selected_fw_id = self.sd_listbox.get(sel[0])
        except Exception:
            return

        if not messagebox.askyesno("Xác nhận", f"Bạn có chắc muốn XOÁ '{selected_fw_id}' khỏi thẻ SD? (Sẽ xoá file và cập nhật index)"):
            return

        # 1. Xoá thư mục file trên thẻ
        dest_dir = os.path.join(sd_root, selected_fw_id)
        if os.path.isdir(dest_dir):
            try:
                shutil.rmtree(dest_dir)
                self.log_msg(f"Đã xoá thư mục: {dest_dir}")
            except Exception as e:
                messagebox.showerror("Lỗi xoá", f"Không thể xoá thư mục {selected_fw_id} trên thẻ: {e}")
                # Vẫn tiếp tục để xoá khỏi index
        else:
             self.log_msg(f"Thư mục {dest_dir} không tồn tại, chỉ xoá khỏi index.")

        # 2. Cập nhật index.txt
        original_count = len(self.sd_index_data)
        self.sd_index_data = [item for item in self.sd_index_data if item.get("fw_id") != selected_fw_id]
        new_count = len(self.sd_index_data)

        if original_count == new_count:
             self.log_msg(f"Không tìm thấy '{selected_fw_id}' trong index.txt. Đang lưu lại...")

        # 3. Lưu lại index.txt
        if self.save_sd_index():
            self.log_msg(f"Đã xoá '{selected_fw_id}' khỏi index.txt.")
            self.load_sd_content() # Tải lại danh sách SD
    # ---------------- Encryption tab (New) ----------------
    def on_export_git(self):
        # 1. Lấy danh sách chọn
        selections = self.fw_listbox.curselection()
        if not selections: return messagebox.showinfo("Info", "Vui lòng chọn firmware cần xuất.")

        # 2. Init Tool Mã Hóa (lazy load)
        if not _load_crypto():
            return messagebox.showerror("Lỗi", "Thiếu thư viện Crypto.")
        if FWEncryptor is None:
            return messagebox.showerror("Lỗi", "Không tìm thấy esp_encryptor.py")
        try:
            tool = FWEncryptor(self.enc_key.get(), self.enc_iv.get())
        except ValueError as e:
            return messagebox.showerror("Lỗi Key/IV", str(e))

        root_out_dir = os.path.join(os.getcwd(), "_release_for_git")
        if not os.path.exists(root_out_dir): os.makedirs(root_out_dir)

        git_index_data = []
        count = 0

        # 3. VÒNG LẶP XỬ LÝ
        for idx in selections:
            fw_name = self.fw_listbox.get(idx)
            src_dir = os.path.join(LIB_ROOT, fw_name)

            # [A] Đọc Metadata gốc từ Local
            try:
                with open(os.path.join(src_dir, LOCAL_META_FILE), "r") as f:
                    meta_export = json.load(f) # Copy ra biến tạm để sửa đổi
            except:
                self.log_msg(f"Skip metadata: {fw_name}")
                continue

            # [B] Tạo thư mục con chứa file .enc
            fw_out_dir = os.path.join(root_out_dir, fw_name)
            if os.path.exists(fw_out_dir): shutil.rmtree(fw_out_dir)
            os.makedirs(fw_out_dir, exist_ok=True)

            self.log_msg(f"--- Processing: {fw_name} ---")

            # Danh sách mapping: Tên file gốc -> Tên trường Path -> Tên trường MD5
            files_map = [
                ("FW.bin",   "path",            "md5"),
                ("BOTL.bin", "path_bootloader", "md5_bootloader"),
                ("PART.bin", "path_partition",  "md5_partition")
            ]

            for fname, key_path, key_md5 in files_map:
                src_path = os.path.join(src_dir, fname)

                if os.path.exists(src_path):
                    dest_fname = fname.replace(".bin", ".enc")
                    dest_path = os.path.join(fw_out_dir, dest_fname)

                    # --- LOGIC TÍNH TOÁN & MÃ HÓA ---
                    with open(src_path, "rb") as f_in: raw = f_in.read()

                    # 1. Tạo dữ liệu đã Padding (Đây là dữ liệu thực tế sẽ nằm trên thẻ SD)
                    padded_data = pad(raw, AES.block_size)

                    # 2. Tính MD5 của dữ liệu ĐÃ PADDING
                    final_md5 = hashlib.md5(padded_data).hexdigest()

                    # 3. Mã hóa và ghi file .enc ra đĩa
                    # (Hàm encrypt_data bên trong cũng tự pad giống hệt bước 1)
                    enc_data = tool.encrypt_data(raw)
                    with open(dest_path, "wb") as f_out: f_out.write(enc_data)

                    self.log_msg(f"  OK: {dest_fname}")

                    # 4. CẬP NHẬT METADATA CHO GIT
                    # - Path: Trỏ đến file .enc (để ESP32 tải)
                    # - MD5: Ghi đè bằng MD5 đã padding (để ESP32 so sánh khớp)
                    meta_export[key_path] = f"{fw_name}/{dest_fname}"
                    meta_export[key_md5] = final_md5

                else:
                    # Dọn dẹp nếu không có file
                    meta_export[key_path] = ""
                    meta_export[key_md5] = ""

            # Thêm vào danh sách tổng
            git_index_data.append(meta_export)
            count += 1

        # 4. GHI FILE INDEX.TXT
        index_path = os.path.join(root_out_dir, "index.txt")
        with open(index_path, "w", encoding="utf-8") as f:
            json.dump(git_index_data, f, indent=2, ensure_ascii=False)

        self.log_msg(f"===> XONG! Đã xuất {count} FW (MD5 chuẩn Padding).")
        # try: os.startfile(root_out_dir)
        # except: pass
# ---------------- Git Push tab (New) ----------------
    def on_push_to_git(self):
        self.on_export_git()  # Đảm bảo đã xuất trước khi push
        repo_url = self.git_repo_url.get().strip()
        if not repo_url: return messagebox.showerror("Lỗi", "Chưa nhập Repo URL!")

        target_dir = os.path.join(os.getcwd(), "_release_for_git")
        if not os.path.isdir(target_dir):
             return messagebox.showerror("Lỗi", "Chưa có folder '_release_for_git'. Hãy chạy Export trước.")

        final_url = repo_url
        if "https://" in repo_url and "@" not in repo_url:
            # Tách lấy username từ link (Logic đơn giản: lấy cụm sau https://github.com/)
            try:
                parts = repo_url.split("github.com/")
                if len(parts) > 1:
                    user_part = parts[1].split("/")[0] # Lấy "PPhucNAME"
                    final_url = repo_url.replace("https://", f"https://{user_part}@")
            except: pass

        # Nội dung file Batch tự động Push
        bat_content = f"""@echo off
                        title AUTO PUSH FIRMWARE TO GIT
                        color 0A
                        echo ==========================================
                        echo      DANG DAY CODE LEN GITHUB...
                        echo ==========================================
                        echo Repo: {final_url}
                        echo.
                        cd /d "{target_dir}"
                        if not exist .git (
                            echo [Init Git]...
                            git init
                            git branch -M main
                        )
                        echo [Add Files]...
                        git add -A
                        echo [Commit]...
                        git commit -m "Auto update via FlashPorter Tool"
                        echo [Remote Config]...
                        git remote remove origin >nul 2>&1
                        git remote add origin {final_url}
                        echo [Pushing]...
                        echo (Neu la lan dau, Windows se hien popup dang nhap)
                        git push -u origin main --force
                        echo.
                        echo HOAN TAT!
                        pause
                        """
        bat_path = os.path.join(target_dir, "auto_push.bat")
        try:
            with open(bat_path, "w") as f: f.write(bat_content)
            self.log_msg("Đang mở CMD để đẩy code...")
            os.startfile(bat_path)
        except Exception as e:
            messagebox.showerror("Lỗi", str(e))

    # ---------------- Utilities ----------------
    def log_msg(self, msg):
        self.log.insert(tk.END, str(msg) + "\n")
        self.log.see(tk.END)
    def load_settings(self):
        """Đọc file cấu hình khi mở App"""
        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, "r") as f:
                    data = json.load(f)
                    # Trả về data, nếu thiếu trường nào thì lấy mặc định
                    return data
            except: pass
        return {}

    def save_settings(self):
        """Lưu cấu hình hiện tại xuống file"""
        data = {
            "key": self.enc_key.get(),
            "iv": self.enc_iv.get(),
            "sd_path": self.sd_path_var.get(),
            "repo_url": self.git_repo_url.get(),
            # Cloud settings
            "cloud_url": self.cloud_base_url.get(),
            "cloud_user": self.cloud_username.get(),
            "cloud_pass": self.cloud_password.get()
        }
        try:
            with open(CONFIG_FILE, "w") as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            print(f"Lỗi lưu config: {e}")
        self.log_msg("Đã lưu cấu hình.")

    # --- HÀM MỚI: QUÉT DỮ LIỆU ĐỂ GỢI Ý ---
    def get_library_suggestions(self):
        """Quét thư viện để lấy danh sách ID và Device Type đã có."""
        existing_ids = []
        existing_types = set() # Dùng set để loại bỏ trùng lặp

        if os.path.exists(LIB_ROOT):
            for name in os.listdir(LIB_ROOT):
                path = os.path.join(LIB_ROOT, name)
                if os.path.isdir(path):
                    existing_ids.append(name)
                    # Đọc metadata để lấy Device Type
                    meta_path = os.path.join(path, LOCAL_META_FILE)
                    if os.path.isfile(meta_path):
                        try:
                            with open(meta_path, "r", encoding="utf-8") as f:
                                data = json.load(f)
                                if "device_type" in data:
                                    existing_types.add(data["device_type"])
                        except: pass

        return sorted(existing_ids), sorted(list(existing_types))

    def on_id_selected(self, event):
        """Khi chọn ID cũ từ list -> Tự động điền Device Type & Version cũ."""
        selected_id = self.combo_id.get()
        path = os.path.join(LIB_ROOT, selected_id)
        meta_path = os.path.join(path, LOCAL_META_FILE)

        if os.path.isfile(meta_path):
            try:
                with open(meta_path, "r", encoding="utf-8") as f:
                    data = json.load(f)

                # Tự động điền Device Type
                self.add_device_type_var.set(data.get("device_type", ""))

                # Gợi ý version kế tiếp (Logic đơn giản: lấy ver cũ thêm chữ OLD)
                old_ver = data.get("version", "0.0.0")
                self.add_version_var.set(old_ver)

                # Đổi màu nút để báo hiệu là "Cập nhật"
                self.btn_add.configure(text="🔄 CẬP NHẬT FIRMWARE NÀY", style="Primary.TButton")

            except: pass
        else:
            # Nếu là ID mới
            self.btn_add.configure(text="➕ THÊM MỚI FIRMWARE", style="Primary.TButton")

    # ================== CLOUD SYNC TAB ==================
    def _build_cloud_tab(self, parent):
        """Xây dựng giao diện Tab Sync Cloud."""
        frm = ttk.Frame(parent, padding=15)
        frm.pack(fill=tk.BOTH, expand=True)

        # ==================================================
        # GROUP 1: ĐĂNG NHẬP CLOUD
        # ==================================================
        grp_login = ttk.LabelFrame(frm, text="1. Đăng nhập Server", padding=10)
        grp_login.pack(fill=tk.X, pady=(0, 10))
        grp_login.columnconfigure(1, weight=1)

        ttk.Label(grp_login, text="Server URL:").grid(row=0, column=0, sticky="w", pady=3)
        ttk.Entry(grp_login, textvariable=self.cloud_base_url, font=("Consolas", 9)).grid(row=0, column=1, padx=5, sticky="ew")

        ttk.Label(grp_login, text="Username:").grid(row=1, column=0, sticky="w", pady=3)
        ttk.Entry(grp_login, textvariable=self.cloud_username, font=("Consolas", 9)).grid(row=1, column=1, padx=5, sticky="ew")

        ttk.Label(grp_login, text="Password:").grid(row=2, column=0, sticky="w", pady=3)
        ttk.Entry(grp_login, textvariable=self.cloud_password, show="*", font=("Consolas", 9)).grid(row=2, column=1, padx=5, sticky="ew")

        btn_row = ttk.Frame(grp_login)
        btn_row.grid(row=3, column=0, columnspan=2, pady=10)
        ttk.Button(btn_row, text="🔐 Đăng nhập", command=self.cloud_login).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_row, text="💾 Lưu thông tin", command=self.cloud_save_credentials).pack(side=tk.LEFT, padx=5)

        self.cloud_status_label = ttk.Label(grp_login, text="Chưa đăng nhập", foreground="gray")
        self.cloud_status_label.grid(row=4, column=0, columnspan=2)

        # ==================================================
        # GROUP 2: CHỌN FIRMWARE TỪ CLOUD
        # ==================================================
        grp_select = ttk.LabelFrame(frm, text="2. Chọn Firmware từ Cloud", padding=10)
        grp_select.pack(fill=tk.BOTH, expand=True, pady=(0, 10))

        # Row 1: Customer dropdown
        row1 = ttk.Frame(grp_select)
        row1.pack(fill=tk.X, pady=5)
        ttk.Label(row1, text="Customer:").pack(side=tk.LEFT)
        self.cloud_customer_combo = ttk.Combobox(row1, state="readonly", width=40)
        self.cloud_customer_combo.pack(side=tk.LEFT, padx=10, fill=tk.X, expand=True)
        self.cloud_customer_combo.bind("<<ComboboxSelected>>", self.cloud_load_firmware)
        ttk.Button(row1, text="🔄", width=3, command=self.cloud_load_customers).pack(side=tk.LEFT)

        # Row 2: Firmware list
        ttk.Label(grp_select, text="Firmware Versions:").pack(anchor="w", pady=(10, 5))

        # Treeview để hiển thị firmware
        columns = ("version", "description", "status", "files")
        self.cloud_fw_tree = ttk.Treeview(grp_select, columns=columns, show="headings", height=8)
        self.cloud_fw_tree.heading("version", text="Version")
        self.cloud_fw_tree.heading("description", text="Mô tả")
        self.cloud_fw_tree.heading("status", text="Status")
        self.cloud_fw_tree.heading("files", text="Files")
        self.cloud_fw_tree.column("version", width=150)
        self.cloud_fw_tree.column("description", width=200)
        self.cloud_fw_tree.column("status", width=80)
        self.cloud_fw_tree.column("files", width=250)
        self.cloud_fw_tree.pack(fill=tk.BOTH, expand=True, pady=5)

        # ==================================================
        # GROUP 3: ACTIONS
        # ==================================================
        grp_action = ttk.Frame(frm)
        grp_action.pack(fill=tk.X, pady=10)

        ttk.Button(grp_action, text="⬇️ TẢI VỀ THƯ VIỆN", style="Primary.TButton",
                   width=25, command=self.cloud_download_to_library).pack(side=tk.RIGHT, padx=5)
        ttk.Button(grp_action, text="⬇️ Tải TẤT CẢ về", width=20,
                   command=self.cloud_download_all).pack(side=tk.RIGHT, padx=5)

    def cloud_login(self):
        """Đăng nhập vào Cloud Server."""
        import urllib.request
        import urllib.parse

        base_url = self.cloud_base_url.get().strip().rstrip('/')
        username = self.cloud_username.get().strip()
        password = self.cloud_password.get().strip()

        if not all([base_url, username, password]):
            return messagebox.showerror("Lỗi", "Vui lòng điền đầy đủ thông tin đăng nhập.")

        self.log_msg(f"[Cloud] Đang đăng nhập: {username}@{base_url}...")
        self.cloud_status_label.config(text="Đang đăng nhập...", foreground="orange")
        self.update()

        try:
            # POST login
            login_url = f"{base_url}/login"
            data = urllib.parse.urlencode({"username": username, "password": password}).encode()

            req = urllib.request.Request(login_url, data=data, method='POST')
            req.add_header('Content-Type', 'application/x-www-form-urlencoded')

            # Tạo opener để bắt cookies
            import http.cookiejar
            cookie_jar = http.cookiejar.CookieJar()
            opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cookie_jar))

            response = opener.open(req)

            # Tìm token trong cookies
            for cookie in cookie_jar:
                if cookie.name == "access_token":
                    self.cloud_token = cookie.value
                    break

            if self.cloud_token:
                self.cloud_status_label.config(text=f"✅ Đã đăng nhập: {username}", foreground="green")
                self.log_msg(f"[Cloud] Đăng nhập thành công!")
                # Tự động load customers
                self.cloud_load_customers()
            else:
                self.cloud_status_label.config(text="❌ Không nhận được token", foreground="red")
                self.log_msg("[Cloud] Lỗi: Không nhận được access_token từ server.")

        except urllib.error.HTTPError as e:
            self.cloud_status_label.config(text=f"❌ Lỗi {e.code}", foreground="red")
            self.log_msg(f"[Cloud] Lỗi đăng nhập: HTTP {e.code} - {e.reason}")
        except Exception as e:
            self.cloud_status_label.config(text="❌ Lỗi kết nối", foreground="red")
            self.log_msg(f"[Cloud] Lỗi: {e}")

    def cloud_save_credentials(self):
        """Lưu thông tin đăng nhập cloud."""
        self.save_settings()
        self.log_msg("[Cloud] Đã lưu thông tin đăng nhập.")

    def cloud_load_customers(self):
        """Load danh sách customers từ API."""
        import urllib.request

        if not self.cloud_token:
            return messagebox.showwarning("Chưa đăng nhập", "Vui lòng đăng nhập trước.")

        base_url = self.cloud_base_url.get().strip().rstrip('/')

        try:
            req = urllib.request.Request(f"{base_url}/api/customers")
            req.add_header('Cookie', f'access_token={self.cloud_token}')

            with urllib.request.urlopen(req) as response:
                data = json.loads(response.read().decode())

            self.cloud_customers = [c.get("customer_id", "") for c in data]
            self.cloud_customer_combo['values'] = self.cloud_customers

            if self.cloud_customers:
                self.cloud_customer_combo.set(self.cloud_customers[0])
                self.log_msg(f"[Cloud] Đã tải {len(self.cloud_customers)} customers.")
                self.cloud_load_firmware()  # Tự động load firmware của customer đầu tiên
            else:
                self.log_msg("[Cloud] Không có customer nào.")

        except Exception as e:
            self.log_msg(f"[Cloud] Lỗi load customers: {e}")

    def cloud_load_firmware(self, event=None):
        """Load danh sách firmware versions cho customer được chọn."""
        import urllib.request

        customer_id = self.cloud_customer_combo.get()
        if not customer_id or not self.cloud_token:
            return

        base_url = self.cloud_base_url.get().strip().rstrip('/')

        # Clear tree
        for item in self.cloud_fw_tree.get_children():
            self.cloud_fw_tree.delete(item)
        self.cloud_firmware_list = []

        try:
            req = urllib.request.Request(f"{base_url}/api/firmware/{customer_id}")
            req.add_header('Cookie', f'access_token={self.cloud_token}')

            with urllib.request.urlopen(req) as response:
                data = json.loads(response.read().decode())

            self.cloud_firmware_list = data

            for fw in data:
                version = fw.get("version", "")
                description = fw.get("description", "")
                status = fw.get("status", "")
                files = fw.get("files", [])
                files_str = ", ".join([f.get("filename", "") if isinstance(f, dict) else f for f in files])

                self.cloud_fw_tree.insert("", "end", values=(version, description, status, files_str))

            self.log_msg(f"[Cloud] Đã tải {len(data)} firmware cho '{customer_id}'.")

        except Exception as e:
            self.log_msg(f"[Cloud] Lỗi load firmware: {e}")

    def cloud_download_to_library(self):
        """Download firmware được chọn về thư viện local (theo flow FlashPorter)."""
        import urllib.request
        from urllib.parse import quote

        # 1. Kiểm tra đã chọn firmware chưa
        selection = self.cloud_fw_tree.selection()
        if not selection:
            return messagebox.showinfo("Info", "Vui lòng chọn firmware cần tải.")

        if not self.cloud_token:
            return messagebox.showwarning("Chưa đăng nhập", "Vui lòng đăng nhập trước.")

        customer_id = self.cloud_customer_combo.get()
        if not customer_id:
            return messagebox.showerror("Lỗi", "Chưa chọn Customer.")

        base_url = self.cloud_base_url.get().strip().rstrip('/')

        # 2. Lấy thông tin firmware được chọn
        item = selection[0]
        values = self.cloud_fw_tree.item(item, "values")
        version = values[0]

        # Tìm firmware data
        fw_data = None
        for fw in self.cloud_firmware_list:
            if fw.get("version") == version:
                fw_data = fw
                break

        if not fw_data:
            return messagebox.showerror("Lỗi", "Không tìm thấy thông tin firmware.")

        files = fw_data.get("files", [])
        if not files:
            return messagebox.showerror("Lỗi", "Firmware không có file nào.")

        # 3. Tạo fw_id ngắn (FAT32 8.3 compatible): FW00001, FW00002...
        fw_id = generate_short_fw_id()
        dest_dir = os.path.join(LIB_ROOT, fw_id)
        os.makedirs(dest_dir, exist_ok=True)

        self.log_msg(f"[Cloud] Đang tải firmware: {customer_id}/{version}...")

        try:
            # 4. Download và phân loại files
            app_file = None
            boot_file = None
            part_file = None

            for file_info in files:
                filename = file_info.get("filename", "") if isinstance(file_info, dict) else file_info
                address = file_info.get("address", "0x10000") if isinstance(file_info, dict) else "0x10000"

                # Download file
                download_url = f"{base_url}/firmware/{customer_id}/{version}/download/{quote(filename)}"
                req = urllib.request.Request(download_url)
                req.add_header('Cookie', f'access_token={self.cloud_token}')

                local_temp = os.path.join(dest_dir, filename)
                self.log_msg(f"  -> Đang tải: {filename}...")

                with urllib.request.urlopen(req) as response:
                    with open(local_temp, 'wb') as f:
                        f.write(response.read())

                # Phân loại theo address hoặc tên file
                fname_lower = filename.lower()
                if address == "0x01000" or "bootloader" in fname_lower:
                    boot_file = local_temp
                elif address == "0x8000" or "partition" in fname_lower:
                    part_file = local_temp
                else:  # 0x10000 hoặc file còn lại -> FW (application)
                    app_file = local_temp

            # 5. Kiểm tra đủ 3 file
            if not all([app_file, boot_file, part_file]):
                missing = []
                if not app_file: missing.append("Application")
                if not boot_file: missing.append("Bootloader")
                if not part_file: missing.append("Partition")
                raise Exception(f"Thiếu file: {', '.join(missing)}")

            # 6. Rename theo chuẩn FlashPorter
            dest_app = os.path.join(dest_dir, "FW.bin")
            dest_boot = os.path.join(dest_dir, "BOTL.bin")
            dest_part = os.path.join(dest_dir, "PART.bin")

            if app_file != dest_app:
                shutil.move(app_file, dest_app)
            if boot_file != dest_boot:
                shutil.move(boot_file, dest_boot)
            if part_file != dest_part:
                shutil.move(part_file, dest_part)

            # 7. Tính MD5
            md5_app = calculate_md5(dest_app)
            md5_boot = calculate_md5(dest_boot)
            md5_part = calculate_md5(dest_part)

            if not all([md5_app, md5_boot, md5_part]):
                raise Exception("Không thể tính MD5.")

            # 8. Tạo metadata theo chuẩn FlashPorter (rút gọn cho màn hình nhỏ)
            metadata = {
                "fw_id": fw_id,
                "device_type": shorten_device_type(customer_id),  # VENDING_MINI_DIEN_DUNG -> VMDD
                "version": shorten_version(version),  # v1.1.2_XXX -> v1.1
                "path": f"/{fw_id}/FW.bin",
                "md5": md5_app,
                "path_bootloader": f"/{fw_id}/BOTL.bin",
                "md5_bootloader": md5_boot,
                "path_partition": f"/{fw_id}/PART.bin",
                "md5_partition": md5_part,
                # Thêm thông tin cloud đầy đủ để tracking
                "cloud_source": {
                    "customer_id": customer_id,
                    "version": version,
                    "description": fw_data.get("description", ""),
                    "status": fw_data.get("status", "")
                }
            }

            # 9. Lưu metadata
            meta_path = os.path.join(dest_dir, LOCAL_META_FILE)
            with open(meta_path, "w", encoding="utf-8") as f:
                json.dump(metadata, f, indent=2, ensure_ascii=False)

            # 10. Cleanup - xóa các file tạm không cần
            for f in os.listdir(dest_dir):
                if f not in ["FW.bin", "BOTL.bin", "PART.bin", LOCAL_META_FILE]:
                    try:
                        os.remove(os.path.join(dest_dir, f))
                    except: pass

            self.log_msg(f"[Cloud] ✅ Đã tải xong: {fw_id}")
            self.refresh_firmware_list()
            messagebox.showinfo("Thành công", f"Đã tải firmware '{fw_id}' vào thư viện!")

        except Exception as e:
            self.log_msg(f"[Cloud] ❌ Lỗi: {e}")
            messagebox.showerror("Lỗi tải firmware", str(e))
            # Cleanup nếu lỗi
            if os.path.exists(dest_dir):
                shutil.rmtree(dest_dir)

    def cloud_download_all(self):
        """Tải tất cả firmware của customer hiện tại."""
        if not self.cloud_firmware_list:
            return messagebox.showinfo("Info", "Không có firmware nào để tải.")

        count = len(self.cloud_firmware_list)
        if not messagebox.askyesno("Xác nhận", f"Bạn muốn tải TẤT CẢ {count} firmware về thư viện?"):
            return

        # Select all items in tree
        for item in self.cloud_fw_tree.get_children():
            self.cloud_fw_tree.selection_add(item)

        # Download từng cái
        success = 0
        for item in self.cloud_fw_tree.get_children():
            self.cloud_fw_tree.selection_set(item)
            try:
                self.cloud_download_to_library()
                success += 1
            except Exception as e:
                self.log_msg(f"[Cloud] Lỗi tải: {e}")

        self.log_msg(f"[Cloud] Hoàn tất: {success}/{count} firmware.")
        messagebox.showinfo("Hoàn tất", f"Đã tải {success}/{count} firmware.")

if __name__ == "__main__":
    app = App()
    app.mainloop()
