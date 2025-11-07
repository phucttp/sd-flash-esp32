# 🚀 ESP32 Offline Flasher

**ESP32 Offline Flasher** biến một bo **ESP32 (Host)** thành một thiết bị **nạp firmware di động**, **không cần máy tính**.  
Bạn có thể chọn firmware từ **menu OLED** và nạp cho một **ESP32 khác (Target)** qua UART.

---

## 🌟 Tính năng

Dự án này là một giải pháp hoàn chỉnh để cập nhật firmware **“tại chỗ” (in-the-field):**

- 📱 **Menu OLED:** Giao diện menu tương tác trên màn hình SSD1306 (128x32).
- 🗃️ **Nạp từ Thẻ SD:** Đọc danh sách firmware động từ file `index.txt` (định dạng JSON) trên thẻ SD.
- ⚡ **Flash Nhanh:** Sử dụng thư viện `espressif/esp-serial-flasher` để nạp cho ESP32 Target qua UART tốc độ cao.
- 🕹️ **Điều khiển 3 nút:** Dễ dàng điều hướng menu với các nút **UP**, **DOWN**, và **OK** (có debounce).
- 🔍 **Xác thực MD5 (Tùy chọn):** Kiểm tra tính toàn vẹn firmware sau khi nạp nếu có thông tin MD5 trong `index.txt`.
- 📊 **Phản hồi Trực quan:** Hiển thị trạng thái (Booting, Flashing, Progress, Success, Error) trên OLED.
- 📡 **UART Monitor:** Tự động tạo task để lắng nghe và in log từ Target sau khi nạp xong.

---

## 🛠️ Phần cứng Yêu cầu

### 1️⃣ Host (Thiết bị Nạp)

Thiết bị chạy code này:

- **Vi điều khiển:** ESP32 (thiết kế cho ESP32-C3 hoặc S3)
- **Màn hình OLED:** SSD1306 I2C (128x32)  
  - SDA → *(I2C SDA)*  
  - SCL → *(I2C SCL)*
- **Lưu trữ:** Thẻ MicroSD qua SPI  
  - CS → GPIO 7  
  - MOSI, MISO, SCK → *(SPI Pins)*
- **Điều khiển:** 3 nút nhấn (kéo lên VCC, nhấn xuống GND)  
  - BTN_UP → GPIO 21  
  - BTN_DOWN → GPIO 20  
  - BTN_OK → GPIO 10

### 2️⃣ Target (Thiết bị được Nạp)

ESP32 cần nạp firmware.

### 3️⃣ Kết nối Host ↔ Target

| Host (Nạp) | Target (Được Nạp) | Chức năng              |
|-------------|--------------------|-------------------------|
| GPIO 0 (UART1_TX) | RXD0 | Gửi dữ liệu firmware |
| GPIO 1 (UART1_RX) | TXD0 | Nhận phản hồi / log |
| GPIO 2 | EN / RESET | Reset Target |
| GPIO 3 | GPIO0 / BOOT | Đưa vào chế độ nạp |

---

## ⚙️ Cách Hoạt động

1. **Khởi động:** ESP32 khởi động OLED và mount thẻ SD.  
2. **Đọc Metadata:** Mở file `/index.txt` trên thẻ SD.  
3. **Xây dựng Menu:** Phân tích JSON, tạo danh sách firmware (thêm mục *Exit* tự động).  
4. **Hiển thị Menu:** Cho phép người dùng cuộn và chọn firmware.  
5. **Chọn Firmware:** Khi nhấn OK, hệ thống lấy thông tin firmware tương ứng (đường dẫn, MD5, ...).  
6. **Vào Chế độ Nạp:** Điều khiển EN và BOOT của Target để kích hoạt Bootloader.  
7. **Nạp Firmware:** Đọc từng phần `.bin` từ thẻ SD và ghi vào Target qua UART, hiển thị tiến trình trên OLED.  
8. **Xác thực (tùy chọn):** Nếu có MD5, hệ thống xác thực dữ liệu sau khi nạp.  
9. **Hoàn tất:** Target được reset, chạy firmware mới. Hiển thị “✅ Success” và bắt đầu **task UART monitor** để xem log.

---

## 📂 Cấu trúc Thẻ SD

Thẻ SD (FAT32) cần có các file sau:

- Các file firmware `.bin`
- File `index.txt` ở thư mục gốc

### 🧩 Ví dụ `index.txt`

```json
[
  {
    "fw_id": "FW_S3_V1.0",
    "device_type": "ESP32-S3",
    "version": "1.0.0",
    "path": "/firmware/s3_app_v1.bin",
    "md5": "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4",
    "path_bootloader": "/firmware/bootloader.bin",
    "md5_bootloader": "b1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4",
    "path_partition": "/firmware/partitions.bin",
    "md5_partition": "c1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4"
  },
  {
    "fw_id": "FW_C3_V2.1",
    "device_type": "ESP32-C3",
    "version": "2.1.0",
    "path": "/app_c3_v21.bin",
    "md5": "d1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4",
    "path_bootloader": "/firmware/bootloader.bin",
    "md5_bootloader": "b1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4",
    "path_partition": "/firmware/partitions.bin",
    "md5_partition": "c1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4"
  }
]
