# Lộ Trình Dự Án - ESP32 Multi-Flasher

## Trạng Thái Hiện Tại

**Phiên bản:** 1.2.0
**Trạng thái:** MVP + STM32 SWD flash hoạt động
**Cập nhật:** 2026-02-16

---

## ✅ Kết Nối Universal: Software Brute-Force (v1.0.1)

**Trạng thái:** HOÀN THÀNH
**Độ ưu tiên:** QUAN TRỌNG
**Hoàn thành:** 2026-01-30

### Vấn đề ban đầu

Flasher gặp vấn đề tương thích với các loại devkit khác nhau:
- Logic GPIO đảo ngược tùy board
- Timing khác nhau (RC filter, capacitor)
- esptool dùng USB-UART chip có circuit chuẩn, FlashPod nối trực tiếp GPIO

### Giải pháp: Software Brute-Force

Thử tất cả 12 tổ hợp GPIO logic + timing cho đến khi kết nối thành công:

```cpp
// 4 GPIO combinations x 3 timing profiles = 12 lần thử
const int combinations[4][2] = {
    {1, 1},  // RST đảo, BOOT đảo
    {1, 0},  // RST đảo, BOOT thường
    {0, 1},  // RST thường, BOOT đảo
    {0, 0},  // RST thường, BOOT thường
};
const uint32_t timings[3][2] = {{100, 50}, {100, 100}, {200, 200}};
```

### Animation khi kết nối

```
┌────────────────────┐
│  Connecting |      │  <- spinner xoay: | / - \
│  Try 5/12...       │  <- đếm số lần thử
└────────────────────┘
```

### Công việc đã hoàn thành

- [x] Implement `try_all_reset_combinations()` với 12 tổ hợp
- [x] Thêm animation spinner + đếm số lần thử
- [x] Test với DevKitC, NodeMCU, ESP32-C3, module trần

---

## Tính Năng Đã Implement (v1.0.0)

### Chức năng chính ✅
- [x] Nạp firmware offline từ thẻ SD
- [x] Giao diện menu OLED với 3 nút điều hướng
- [x] Catalog firmware dạng JSON (index.txt)
- [x] Nạp đa phân vùng (bootloader + partition + app)
- [x] Xác thực MD5 sau khi nạp
- [x] Điều khiển boot Target (EN/BOOT GPIO)
- [x] Restart hệ thống sau mỗi thao tác (quản lý bộ nhớ)

### Đồng bộ Online ✅
- [x] Kết nối WiFi qua WiFiManager
- [x] Captive portal cấu hình WiFi/server
- [x] Tải firmware từ xa (HTTP/HTTPS)
- [x] Giải mã firmware mã hóa AES-128-CBC
- [x] Đồng bộ thông minh (so sánh local vs remote)
- [x] Chế độ xóa sạch (nhấn giữ để xóa tất cả local)
- [x] File cấu hình trên SD (/config/*.txt)

### Tính năng nâng cao ✅
- [x] Chế độ Monitor UART (xem log Target)
- [x] Lệnh xóa chip
- [x] Hiển thị tiến trình trên OLED
- [x] Tích hợp hệ thống log ESP-IDF
- [x] Tương thích Arduino framework
- [x] Hỗ trợ thẻ SD FAT32 (định dạng tên 8.3)

### Hỗ trợ phần cứng ✅
- [x] ESP32-C3 làm Host chính
- [x] OLED SH1106G (128x64, I2C)
- [x] Thẻ SD (chế độ SPI)
- [x] Chống rung nút nhấn với phát hiện nhấn giữ
- [x] Hidden Tools Menu (UP+DOWN 3 giây)

---

## Hạn Chế Hiện Tại

### Giới hạn phần cứng
- **RAM:** Giới hạn bởi ESP32-C3 (~400KB tổng, ~200KB khả dụng)
- **Thẻ SD:** Chỉ FAT32, bắt buộc định dạng tên 8.3
- **WiFi:** Chỉ 2.4GHz (không hỗ trợ 5GHz)
- **Màn hình:** 128x64 pixel, hiển thị ~6 dòng menu

### Giới hạn phần mềm
- **Không đa luồng:** Vòng lặp Arduino đơn luồng, tất cả thao tác blocking
- **Không lưu trạng thái:** Hệ thống restart sau mỗi thao tác
- **Không phục hồi lỗi:** Nạp bị gián đoạn cần restart thủ công
- **Không ghi log SD:** Tất cả log qua UART (volatile)

### Công cụ FlashPorter
- **Trạng thái:** Đã có, đang phát triển thêm
- **Thay thế:** Chuẩn bị thẻ SD thủ công qua JSON

---

## Lộ Trình

### v1.0.1 - Kết nối Brute-Force (HOÀN THÀNH)

**Độ ưu tiên:** QUAN TRỌNG
**Trạng thái:** HOÀN THÀNH (2026-01-30)

#### Thay đổi code (Đã xong)
- [x] Implement `try_all_reset_combinations()` - brute force 12 tổ hợp GPIO
- [x] Thêm animation kết nối (spinner + đếm số lần thử)
- [x] Test với DevKitC, NodeMCU, ESP32-C3, module trần

---

## ✅ STM32 SWD Flash Programming (v1.2.0)

**Trạng thái:** HOÀN THÀNH
**Độ ưu tiên:** QUAN TRỌNG
**Hoàn thành:** 2026-02-16

### Tính năng

Nạp firmware STM32 (F4 family) qua giao thức SWD, không cần ST-Link hay USB.
ESP32-C3 bit-bang SWD trực tiếp qua GPIO (SWDIO=GPIO0, SWCLK=GPIO3).

### Chức năng đã implement

- [x] SWD connect + chip detect (IDCODE, DBGMCU, flash size)
- [x] RDP Level detect (Level 0/1/2)
- [x] RDP Level 1 → Level 0 disable (blind writes + mass erase)
- [x] RDP disable verify + rescue erase (zombie state handling)
- [x] Flash firmware: erase sectors → program 256B chunks → on-the-fly verify
- [x] Retry strategy: 3 retries/chunk + 3 full re-flash attempts
- [x] RAM buffering: read entire FW from SD → close SD → flash (no SPI interference)
- [x] OLED progress: "Flash X/Y" chunk count + percentage
- [x] RDP-locked OLED message: "Erase STM32 first"
- [x] detect_rdp() warm-up before flash (improves SWD connection without reset)
- [x] Adafruit_DAP_STM32 select() fix: delay + re-halt after SYSRESETREQ

### Bugs phát hiện và sửa

- [x] **3x blind write retry corruption**: KEY state machine lock khi KEY2 drop
  - Fix: SYSRESETREQ between retry attempts
- [x] **GPIO2/FSPIQ conflict**: SPI MISO pin corruption during SWD
  - Fix: SWDIO moved to GPIO0
- [x] **CSW AddrInc → FLASH_CR LOCK**: flash_busy() reads corrupt TAR
  - Fix: Remove flash_busy() from programBlock inner loop
- [x] **SPRMOD bit 31**: 0xFFFF<<16 enables PCROP instead of disabling WRP
  - Fix: Use hardcoded 0x0FFFAAEE

### Thư viện sử dụng

- `Adafruit_DAP` (modified for ESP32-C3 compatibility)
- `Adafruit_BusIO` (I2C/SPI abstraction)

### Hạn chế

- Chỉ hỗ trợ STM32F4 family (tested: STM32F411)
- Max firmware size: 128KB (RAM buffer limit trên ESP32-C3)
- RDP disable cần power cycle (rút điện) — STM32F4 không có OBL_LAUNCH
- dap_write_word silent fail ~0.1% — on-the-fly verify bắt buộc
- PHẢI dùng nguồn 3.3V cho STM32 (5V gây lỗi SWD voltage mismatch)

---

### v1.1 - Ổn định & Trải nghiệm (Ngắn hạn)

**Độ ưu tiên:** Cao
**Thời gian:** 1-2 tháng

#### Sửa lỗi & Cải tiến
- [ ] Thêm watchdog timer phát hiện treo máy
- [ ] Logic retry nạp firmware (3 lần thử)
- [ ] Phát hiện pin yếu (nếu dùng pin)
- [ ] Cải thiện thông báo lỗi (phản hồi OLED chi tiết hơn)
- [ ] Animation thanh tiến trình (không chỉ phần trăm)

#### Trải nghiệm người dùng
- [ ] Màn hình khởi động với thông tin phiên bản
- [ ] Phản hồi âm thanh (còi khi thành công/lỗi) - tùy chọn
- [ ] Đèn LED trạng thái (nhấp nháy khi nạp/đồng bộ)
- [ ] Hộp thoại xác nhận cho thao tác nguy hiểm (Xóa, Xóa sạch)
- [ ] Hiển thị firmware vừa nạp trong menu

#### Tài liệu
- [ ] Hướng dẫn khắc phục sự cố chi tiết với hình ảnh
- [ ] Video hướng dẫn thiết lập và sử dụng
- [ ] Tài liệu và bản phát hành FlashPorter
- [ ] Sơ đồ mạch kết nối phần cứng
- [ ] BOM (Danh sách vật liệu) cho sản xuất

---

### v1.2 - Tính năng nâng cao (Trung hạn)

**Độ ưu tiên:** Trung bình
**Thời gian:** 3-4 tháng

#### Quản lý Firmware
- [ ] So sánh phiên bản firmware (gợi ý cập nhật tự động)
- [ ] Tính năng rollback (giữ firmware trước)
- [ ] Chế độ nạp hàng loạt (nạp nhiều target tuần tự)
- [ ] Địa chỉ flash tùy chỉnh (không chỉ 0x1000/0x8000/0x10000)
- [ ] Hỗ trợ phân vùng bổ sung (NVS, SPIFFS, v.v.)

#### Log & Chẩn đoán
- [ ] Ghi log vào thẻ SD (log lỗi lưu trữ)
- [ ] Xuất log nạp ra file (timestamp, thao tác, lỗi)
- [ ] Phát hiện loại chip Target (ESP32/C3/S3 tự động)
- [ ] Phát hiện và xác minh kích thước flash
- [ ] Chế độ Monitor với lọc log (chỉ error/warning)

#### Bảo mật nâng cao
- [ ] Secure boot cho Host ESP32-C3
- [ ] Mã hóa flash cho firmware Host
- [ ] Xác thực chứng chỉ HTTPS cho download
- [ ] Mã hóa key AES (lưu trong NVS, không phải SD plaintext)
- [ ] Xác minh chữ ký firmware (chữ ký số)
- [ ] Kiểm soát truy cập (PIN/mật khẩu cho thao tác)

---

### v1.3 - netFlashTTP: Nạp song song đa node (Dài hạn)

**Độ ưu tiên:** Trung bình
**Thời gian:** 6-12 tháng

#### Giao thức netFlashTTP

Hệ thống nạp firmware song song cho nhiều target cùng lúc qua mạng ESP-NOW wireless.

```
                         ┌─────────────────┐
                         │   FlashPod      │
                         │    MASTER       │
                         │  (Controller)   │
                         └────────┬────────┘
                                  │ ESP-NOW (netFlashTTP)
              ┌───────────────────┼───────────────────┐
              │                   │                   │
              ▼                   ▼                   ▼
       ┌────────────┐      ┌────────────┐      ┌────────────┐
       │  Node 1    │      │  Node 2    │      │  Node 3    │
       │  (Slave)   │      │  (Slave)   │      │  (Slave)   │
       │  SD Card   │      │  SD Card   │      │  SD Card   │
       └─────┬──────┘      └─────┬──────┘      └─────┬──────┘
             │                   │                   │
             ▼                   ▼                   ▼
       ┌──────────┐        ┌──────────┐        ┌──────────┐
       │ Target 1 │        │ Target 2 │        │ Target 3 │
       │  ESP32   │        │  ESP32   │        │  ESP32   │
       └──────────┘        └──────────┘        └──────────┘
```

**Đặc điểm:**
- Mỗi node lưu firmware trên SD Card riêng → Flash song song 100%
- Master chỉ gửi lệnh đồng bộ → Bandwidth không phải bottleneck
- Wireless (ESP-NOW) → Không cần nối dây giữa các node
- Mở rộng: 1 Master điều khiển tối đa 20 nodes

#### Giao diện Master OLED

```
┌────────────────────┐
│ netFlashTTP: FW001 │
├────────────────────┤
│ Node1: ████░░ 67%  │
│ Node2: █████░ 83%  │
│ Node3: ██████ XONG │
│ Node4: ░░░░░░ LỖI  │
└────────────────────┘
```

#### Công việc cần làm

- [ ] Thiết kế đặc tả giao thức netFlashTTP
- [ ] Implement giao tiếp ESP-NOW master/slave
- [ ] Tạo cơ chế phát hiện & ghép nối node
- [ ] Build firmware slave (tối giản, không OLED)
- [ ] Thêm hiển thị trạng thái đa node trên Master OLED
- [ ] Implement lệnh nạp broadcast
- [ ] Thêm điều khiển node riêng lẻ (retry, bỏ qua)
- [ ] Thiết kế PCB "Expansion Node"
- [ ] Viết hướng dẫn ghép nối & tài liệu

#### Mô hình kinh doanh

| Sản phẩm | Mô tả | Giá |
|----------|-------|-----|
| FlashPod Lite | Đơn node, bộ kit DIY | 199K |
| FlashPod Standard | Đơn node, đã lắp ráp | 399K |
| FlashPod Pro | Master + 2 nodes | 799K |
| Expansion Node | Node slave bổ sung | 150K |
| Factory Kit | Master + 8 nodes | 2.5M |

---

### v2.0 - Hỗ trợ đa MCU (Tương lai)

**Độ ưu tiên:** Thấp
**Thời gian:** 12-18 tháng

#### Tầm nhìn: Programmer di động đa năng

Mở rộng hỗ trợ nhiều loại MCU, không chỉ ESP32.

| Họ MCU | Giao thức | Độ khó | Trạng thái |
|--------|-----------|--------|------------|
| ESP32/C3/S3/S2 | UART ROM bootloader | ✅ Xong | v1.0 |
| STM32F4 | SWD (bit-bang) | ✅ Xong | v1.2 |
| STM32F0/F1 | SWD (bit-bang) | Trung bình | v2.0 |
| STM32G0/G4 | UART bootloader | Trung bình | v2.0 |
| ATmega328/2560 | STK500v1 (ISP) | Trung bình | v2.1 |
| ATtiny (UPDI) | Giao thức UPDI | Trung bình | v2.1 |
| RP2040 | USB Mass Storage | Khó | Nghiên cứu |
| nRF52 | SWD/JTAG | Khó | Nghiên cứu |

**Điểm bán hàng độc đáo:**
- ✅ Không cần PC (standalone)
- ✅ Đa MCU trong 1 thiết bị
- ✅ Giá rẻ hơn J-Link/công cụ chuyên nghiệp
- ✅ Mã nguồn mở, thân thiện DIY

---

### Tính năng Legacy v1.x

#### Giao diện nâng cao
- [ ] Hỗ trợ OLED lớn hơn (128x64)
- [ ] Giao diện màn hình cảm ứng (tùy chọn)
- [ ] Web UI (chế độ WiFi AP, điều khiển qua trình duyệt)
- [ ] Tích hợp ứng dụng di động (điều khiển BLE)
- [ ] Hỗ trợ đa ngôn ngữ (EN/VN/v.v.)

#### Tính năng sản xuất
- [ ] Chế độ test nhà máy (chuỗi test tự động)
- [ ] Theo dõi số serial (log firmware nào nạp vào thiết bị nào)
- [ ] Quét mã QR để chọn firmware
- [ ] Tích hợp đầu đọc barcode
- [ ] Thống kê sản xuất (tổng số nạp, tỷ lệ thành công)

---

## Nợ Kỹ Thuật

### Chất lượng code
- [ ] Refactor comment tiếng Việt sang tiếng Anh (nhất quán)
- [ ] Chuẩn hóa header guards (`#pragma once` everywhere)
- [ ] Thêm tài liệu Doxygen cho tất cả hàm
- [ ] Implement unit tests (Google Test framework)
- [ ] Tích hợp phân tích tĩnh (Clang-Tidy, Cppcheck)

### Cải tiến kiến trúc
- [ ] Tách lớp trừu tượng phần cứng (HAL)
- [ ] Implement dependency injection (giảm global state)
- [ ] Dùng FreeRTOS tasks (menu + sync chạy đồng thời)
- [ ] Implement kiến trúc hướng sự kiện (message queues)

### Hệ thống build
- [ ] Pipeline CI/CD (GitHub Actions auto-build)
- [ ] Test tự động trên phần cứng (ESP32-C3 test rig)
- [ ] Tự động phát hành binary (GitHub Releases)
- [ ] Cập nhật OTA cho firmware Host
- [ ] Hỗ trợ build đa target (C3/S3/S2)

---

## Mục Tiêu Hiệu Suất

### Mục tiêu v1.1
- Thời gian khởi động: <2 giây (hiện tại: ~2-3s)
- Nạp firmware 1MB: <20 giây (hiện tại: ~30s)
- Đồng bộ 10 firmware: <60 giây (hiện tại: thay đổi)
- Phản hồi menu: <30ms (hiện tại: <50ms)

### Mục tiêu v1.2
- Nạp song song 4 target: <30 giây tổng
- Thời gian phản hồi Web UI: <100ms mỗi thao tác
- Ghi log SD: <10ms mỗi entry (non-blocking)

---

## Chỉ Số Thành Công

### Baseline v1.0
- Tỷ lệ nạp thành công: >95% (mục tiêu: >99%)
- Độ hài lòng người dùng: Chưa đo (mục tiêu: khảo sát trong v1.1)
- Báo lỗi: 0 nghiêm trọng, 2 nhỏ (theo dõi trên GitHub)

### Mục tiêu v1.1
- Tỷ lệ nạp thành công: >99%
- Không có lỗi nghiêm trọng trong production
- 50+ người dùng active (GitHub stars/forks)

### Mục tiêu v1.2
- 1000+ lần nạp firmware được ghi log
- 10+ triển khai sản xuất
- 100+ GitHub stars
- 3+ người đóng góp cộng đồng

---

## Đánh Giá Rủi Ro

### Rủi ro kỹ thuật
- **Rò rỉ bộ nhớ:** Giảm thiểu bằng chiến lược restart, nhưng giới hạn độ phức tạp tính năng
- **Hỏng thẻ SD:** Không có journaling, mất điện khi ghi là thảm họa
- **Độ tin cậy WiFi:** Captive portal có thể fail trên mạng enterprise
- **Bảo mật:** Key AES plaintext trên SD dễ bị tấn công vật lý

### Rủi ro dự án
- **Availability maintainer:** Một maintainer (TTP27), bus factor = 1
- **Tăng trưởng cộng đồng:** User base nhỏ có thể giới hạn feedback và đóng góp
- **Lỗi thời phần cứng:** ESP32-C3 có thể bị thay thế bởi chip mới hơn

---

## Nhật Ký Quyết Định

### Quyết định kiến trúc chính

**Quyết định:** Software brute-force cho boot control
**Ngày:** 2026-01-30
**Lý do:** Direct GPIO control không universal, mỗi devkit có logic/timing khác nhau. Dùng SOFTWARE brute-force thử tất cả 12 tổ hợp GPIO logic + timing.
**Đánh đổi:** Thời gian kết nối 1-5 giây, nhưng KHÔNG cần thay đổi phần cứng
**Trạng thái:** HOÀN THÀNH - Đã implement trong v1.0.1

---

**Quyết định:** Restart sau mỗi thao tác
**Lý do:** Đơn giản hóa quản lý bộ nhớ, tránh rò rỉ
**Đánh đổi:** UX chậm hơn, không lưu trạng thái
**Trạng thái:** Cam kết cho v1.x, xem xét lại trong v2.0

**Quyết định:** Arduino framework + ESP-IDF
**Lý do:** Tận dụng thư viện có sẵn, phát triển dễ hơn
**Đánh đổi:** Binary lớn hơn, chậm hơn IDF thuần
**Trạng thái:** Cam kết cho v1.x, IDF thuần trong v2.0

**Quyết định:** FAT32 cho thẻ SD
**Lý do:** Tương thích universal, implement đơn giản
**Đánh đổi:** Giới hạn tên file 8.3, không có journaling
**Trạng thái:** Cam kết cho v1.x, LittleFS trong v2.0

---

## Lịch Sử Thay Đổi

### v1.2.0 (2026-02-16) - STM32 SWD Flash Programming
- Nạp firmware STM32F4 qua SWD bit-bang (GPIO0/GPIO3)
- RDP detect (Level 0/1/2) + RDP disable (blind writes)
- Flash: erase → program 256B chunks → on-the-fly verify + retry
- Adafruit_DAP library port cho ESP32-C3 (noInterrupts→portENTER_CRITICAL)
- Fix 3x blind write retry: SYSRESETREQ giữa các lần thử
- OLED progress + RDP-locked message
- app_actions wired cho cả ESP32 (UART) và STM32 (SWD)

### v1.0.1 (2026-01-30) - Software Brute-Force
- Kết nối universal với brute-force 12 tổ hợp GPIO
- Animation khi kết nối (spinner + đếm số lần thử)
- Hidden Tools Menu (UP+DOWN 3 giây)
- Loại bỏ item chức năng khỏi menu chính

### v1.0.0 (2025-11-27) - Phát hành đầu tiên
- Nạp firmware offline và online hoàn chỉnh
- Menu OLED với điều hướng nút nhấn
- Đồng bộ WiFi với mã hóa AES-128-CBC
- Chế độ Monitor và xóa chip
- Bộ tài liệu (README, PDR, architecture)

---

**Phiên bản Roadmap:** 1.1
**Đánh giá tiếp theo:** 2026-02-28 (cập nhật hàng tháng)
