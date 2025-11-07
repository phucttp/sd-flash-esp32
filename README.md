# ⚙️ SD Flash ESP32 Project

> 🧠 Dự án thử nghiệm nạp firmware cho ESP32 từ **thẻ SD Card**, sử dụng **ESP-IDF** và **thư viện esp-flasher**.  
> Hệ thống được tổ chức theo mô hình module hóa, dễ mở rộng và bảo trì.

---

## 📂 Cấu trúc thư mục

```plaintext
.
├── 📄 CMakeLists.txt              # File CMake chính của dự án
├── ⚙️ sdkconfig.defaults          # Cấu hình mặc định của ESP-IDF
├── 📁 main/                       # Thư mục chứa mã nguồn chính (component chính)
│   ├── 📄 CMakeLists.txt
│   ├── ⚙️ Kconfig
│   ├── 🧩 main.c / main.cpp       # Logic khởi tạo & vòng lặp chính
│   │
│   ├── 💾 sd_card/                # Module xử lý thẻ SD
│   │   ├── sd_card.h
│   │   └── sd_card.c / sd_card.cpp  # Mount, unmount, đọc & chọn file
│   │
│   ├── 🔐 flash_storage/          # Module quản lý Flash nội bộ (NVS / SPIFFS / FATFS)
│   │   ├── flash_storage.h
│   │   └── flash_storage.c / flash_storage.cpp  # Lưu & đọc firmware từ Flash
│   │
│   ├── ⚡ flasher/                # Module nạp firmware cho ESP32 target
│   │   ├── flasher.h
│   │   └── flasher.cpp            # Logic điều khiển quá trình nạp (dựa trên esp-flasher)
│   │
│   └── 🧱 components.h / includes.h  # (Tùy chọn) Quản lý include chung
│
├── 🧩 components/                 # Các component độc lập (nếu cần)
│   └── 🔧 esp-flasher-lib/        # Thư viện esp-flasher (hoặc tích hợp trực tiếp)
│       ├── CMakeLists.txt
│       └── ...
│
└── 📦 firmware/                   # Chứa firmware mẫu để test
    └── target_firmware.bin
