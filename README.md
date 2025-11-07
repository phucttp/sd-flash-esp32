CẤU TRÚC SƠ BỘ:
.
├── CMakeLists.txt              # File CMake chính của dự án
├── sdkconfig.defaults          # Cài đặt cấu hình mặc định
├── main/                       # Thư mục chứa mã nguồn chính (component chính)
│   ├── CMakeLists.txt
│   ├── Kconfig
│   ├── main.c / main.cpp       # Logic khởi tạo, vòng lặp chính
│   ├── sd_card/                # Module xử lý SD Card
│   │   ├── sd_card.h
│   │   └── sd_card.c / sd_card.cpp  # Logic mount, unmount, đọc file, chọn file
│   ├── flash_storage/          # Module quản lý Flash (NVS/SPIFFS/FATFS)
│   │   ├── flash_storage.h
│   │   └── flash_storage.c / flash_storage.cpp # Logic lưu/đọc firmware từ Flash
│   ├── flasher/                # Module nạp firmware cho target ESP32 (sử dụng esp-flasher)
│   │   ├── flasher.h
│   │   └── flasher.cpp          # Logic điều khiển quá trình nạp (dựa trên thư viện esp-flasher)
│   └── components.h / includes.h # (Tùy chọn) File chung để quản lý các include
├── components/                 # Các component độc lập (nếu cần)
│   └── esp-flasher-lib/        # Thư viện nạp esp-flasher (hoặc tích hợp trực tiếp)
│       ├── CMakeLists.txt
│       └── ...
└── firmware/                   # Thư mục chứa các file firmware test (không thuộc code)
    └── target_firmware.bin