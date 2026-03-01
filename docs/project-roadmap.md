# Lộ Trình Dự Án — ESP MultiFlasher

**Phiên bản:** 1.3.0
**Cập nhật:** 2026-03-01

---

## Trạng Thái Hiện Tại

ESP32-C3 portable firmware flasher. Nạp ESP32 (UART) + STM32F1/F4 (SWD) từ SD card, không cần PC.

### Tính năng đã hoàn thành

| Tính năng | Phiên bản | Trạng thái |
|-----------|-----------|------------|
| ESP32 UART flash (esp-serial-flasher) | v1.0.0 | DONE |
| WiFi sync + AES-128-CBC encrypted download | v1.0.0 | DONE |
| Captive portal cấu hình WiFi/URL | v1.0.0 | DONE |
| OLED menu + 3 nút điều hướng | v1.0.0 | DONE |
| SD card firmware library (JSON metadata) | v1.0.0 | DONE |
| MD5 verify sau flash | v1.0.0 | DONE |
| Monitor UART (xem log Target) | v1.0.0 | DONE |
| Chip erase (ESP32) | v1.0.0 | DONE |
| Software brute-force boot combo (12 tổ hợp) | v1.0.1 | DONE |
| Multi-tab UI (5 tabs: FW, Tools, Desc, Hist, Info) | v1.1.0 | DONE |
| STM32F4 SWD flash + RDP disable (blind write) | v1.2.0 | DONE |
| Adafruit_DAP port cho ESP32-C3 | v1.2.0 | DONE |
| On-the-fly verify + retry (256B chunks) | v1.2.0 | DONE |
| STM32F1 SWD flash (half-word, page erase) | v1.3.0 | DONE |
| IDCODE auto-detect: M3→F1, M4→F4 | v1.3.0 | DONE |
| RDP auto-erase with retry (3x) + confirm dialog | v1.3.0 | DONE |
| Flash history (last 10, tab Hist) | v1.3.0 | DONE |
| UI state persistence (tab + item restore on boot) | v1.3.0 | DONE |
| NetFlash HTTP API (remote flash qua WiFi) | v1.3.0 | DONE |
| FlashPorter PC tool (Python/Tkinter) | v1.3.0 | DONE |

### Target support matrix

| Target | Interface | Engine | Tested on | Status |
|--------|-----------|--------|-----------|--------|
| ESP32 (all variants) | UART | esp-serial-flasher | ESP32-C3, S3, DevKitC | DONE |
| STM32F1 (Cortex-M3) | SWD | FPEC half-word | STM32F103 HD 256KB | DONE |
| STM32F4 (Cortex-M4) | SWD | Flash CR sector | STM32F411 | DONE |

### Kiến trúc hệ thống

```
OLED UI (5 tabs) ──► App Actions ──► Flasher Engines
     │                    │              ├─ ESP32 UART
     │                    │              ├─ STM32F1 SWD
     │                    │              └─ STM32F4 SWD
     │                    │
     ▼                    ▼
  SD Card ◄──────── WiFi/Network
  (metadata,         ├─ Sync Engine
   history,          ├─ NetFlash HTTP API
   config)           └─ Captive Portal
```

---

## Hạn Chế Hiện Tại

### Phần cứng
- **RAM:** ESP32-C3 ~200KB free → FW lớn dùng 32KB streaming (không load toàn bộ)
- **SD card:** FAT32 only, 8.3 filename
- **WiFi:** 2.4GHz only
- **OLED:** 128x64 pixel (6 dòng menu)
- **STM32 SWD:** bit-bang GPIO → ~144μs/word, dap_write_word silent fail ~0.1%

### Phần mềm
- **STM32F1 không tự chạy** sau flash qua SWD → cần power cycle
- **STM32F4 RDP disable** cần power cycle (không có OBL_LAUNCH)
- **Không đa luồng:** tất cả thao tác blocking (Arduino loop)
- **Restart sau thao tác:** quản lý bộ nhớ bằng restart (có UI state restore)

---

## Lộ Trình

### v1.4 — Production & Quality of Life (Ngắn hạn)

**Độ ưu tiên:** Cao

#### Flash engine
- [ ] RDP auto-lock sau flash (Level 0 → Level 1, bảo vệ FW)
- [ ] Mass production mode: flash + auto-lock + verify loop
- [ ] Backup flash content trước khi nạp (rollback cho STM32)
- [ ] Detect flash size tự động (đọc FLASH_SIZE register)

#### UX
- [ ] Error message chi tiết hơn (handshake timeout, erase fail, write fail, verify mismatch)
- [ ] Đèn LED / buzzer trạng thái (tùy chọn)
- [ ] Firmware version compare (gợi ý update)

#### FlashPorter PC tool
- [ ] Batch flash UI (chọn nhiều node NetFlash → flash song song)
- [ ] Auto-discovery LAN nodes (mDNS scan)

---

### v1.5 — Mở rộng MCU support (Trung hạn)

**Độ ưu tiên:** Trung bình

| Họ MCU | Giao thức | Trạng thái |
|--------|-----------|------------|
| STM32G0/G4 | SWD (tương tự F1/F4) | Planned |
| STM32F0 | SWD | Planned |
| ATmega328/2560 | STK500v1 (ISP) | Research |
| ATtiny (UPDI) | UPDI protocol | Research |

#### Hạ tầng
- [ ] CI/CD pipeline (GitHub Actions auto-build)
- [ ] OTA update cho Host firmware
- [ ] Secure boot cho Host ESP32-C3

---

### v2.0 — netFlashTTP: Nạp song song đa node (Dài hạn)

**Độ ưu tiên:** Thấp

Hệ thống nạp firmware song song cho nhiều target qua ESP-NOW wireless.

```
FlashPod MASTER ──ESP-NOW──► Node 1 (SD+Target)
                           ► Node 2 (SD+Target)
                           ► Node 3 (SD+Target)
```

- [ ] Giao thức ESP-NOW master/slave
- [ ] Phát hiện & ghép nối node
- [ ] Firmware slave (tối giản, không OLED)
- [ ] OLED hiển thị trạng thái đa node trên Master
- [ ] PCB "Expansion Node"

---

## Lịch Sử Thay Đổi

### v1.3.0 (2026-03-01) — STM32F1 Engine + RDP Auto-Erase + NetFlash
- STM32F1 SWD engine: FPEC half-word programming, page erase, device whitelist
- IDCODE auto-detect: 0x1BA01477→F1, 0x2BA01477→F4
- RDP auto-erase with 3x retry + confirm dialog trước khi flash
- Flash history tab (last 10 operations)
- UI state persistence (restore tab + item after restart)
- NetFlash HTTP API: remote flash qua WiFi (mDNS discovery)
- FlashPorter: NetFlash client module, theme update

### v1.2.0 (2026-02-16) — STM32F4 SWD Flash Programming
- STM32F4 SWD flash via Adafruit_DAP (bit-bang GPIO)
- RDP detect + blind write disable (mass erase)
- Flash: sector erase → 256B chunks → on-the-fly verify + retry
- Fix: SYSRESETREQ between retry, GPIO2/FSPIQ conflict, CSW AddrInc lock
- OLED progress + RDP-locked message

### v1.1.0 (2026-02-19) — Multi-Tab UX
- 5-tab OLED UI: FW, Tools, Desc, Hist, Info
- Tab navigation (UP+DOWN combo)
- Rename flasher_swd → flasher_swd_stm32f4

### v1.0.1 (2026-01-30) — Software Brute-Force
- Boot combo brute-force (12 tổ hợp GPIO + timing)
- Connection animation (spinner + counter)

### v1.0.0 (2025-11-27) — Initial Release
- ESP32 UART flash offline + online
- WiFi sync + AES-128-CBC encryption
- OLED menu + Monitor + Chip Erase

---

## Quyết Định Kiến Trúc

| Quyết định | Lý do | Đánh đổi |
|------------|-------|----------|
| Arduino + ESP-IDF | Tận dụng thư viện Arduino (Adafruit, WiFiManager) | Binary lớn hơn IDF thuần |
| Restart sau thao tác | Đơn giản hóa quản lý bộ nhớ | UX delay ~2s, cần UI state restore |
| FAT32 SD card | Tương thích universal, dễ chuẩn bị trên PC | 8.3 filename, no journaling |
| SWD bit-bang (GPIO) | Không cần ST-Link, portable | Chậm, ~0.1% write fail → cần verify |
| IDCODE-based routing | Tự động phân biệt F1/F4 không cần user chọn | Chỉ phân biệt ở mức Cortex-M core |
| detect_rdp() warmup | Cải thiện SWD connection thành công | Thêm ~500ms mỗi lần flash |
| Dual RAM strategy | Hỗ trợ FW lớn hơn free RAM | Code phức tạp hơn, SD file open during flash |

---

**Roadmap version:** 2.0
**Đánh giá tiếp theo:** 2026-04-01
