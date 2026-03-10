# Project Overview & PDR — CHIVI FlashPorter TFT

> Last updated: 260307

## 1. Product Summary

**CHIVI FlashPorter TFT** is a standalone firmware flasher built on ESP32-S3. It programs ESP32 (UART) and STM32 (SWD) targets directly from an internal USB FAT drive — no PC software required during flashing.

**Key differentiator:** USB Mass Storage — firmware files are copied via drag-and-drop from PC, then flashed autonomously via 3-button TFT UI.

## 2. Target Users

- Production line operators flashing multiple devices
- Field engineers updating firmware on-site
- Embedded developers needing quick flash without IDE

## 3. Hardware Platform

| Component | Spec |
|-----------|------|
| MCU | ESP32-S3 (Xtensa dual-core, 240MHz) |
| Display | ST7735 TFT 160x128 (SPI, landscape) |
| Storage | Internal FAT partition ~12.9MB via TinyUSB MSC |
| Interface | 3 buttons (UP/DOWN/OK) |
| USB | TinyUSB MSC (drive) + CDC (debug COM port) |

### Pin Map

| Function | GPIO | Notes |
|----------|------|-------|
| TFT MOSI | 34 | Hardware SPI |
| TFT SCLK | 33 | |
| TFT RST | 35 | |
| TFT DC | 36 | |
| TFT BL | 37 | Backlight via MOSFET |
| BTN UP | 4 | Active low |
| BTN DOWN | 1 | Active low |
| BTN OK | 2 | Active low |
| FLASH_PIN_0 | 13 | ESP TX / SWD SWDIO |
| FLASH_PIN_1 | 14 | ESP RX |
| FLASH_PIN_2 | 12 | ESP RESET trigger |
| FLASH_PIN_3 | 11 | ESP BOOT / SWD SWCLK |
| SD CS | 7 | SPI (legacy, unused in TFT build) |

## 4. Supported Targets

| Target Family | Interface | Protocol | Auto-detect |
|---------------|-----------|----------|-------------|
| ESP32 (all variants) | UART | esp-serial-flasher | N/A |
| STM32F1 (Cortex-M3) | SWD | FPEC half-word | IDCODE 0x1BA01477 |
| STM32F4 (Cortex-M4) | SWD | Flash CR sector | IDCODE 0x2BA01477 |

## 5. Core Features

1. **USB Drive firmware store** — PC copies files via USB MSC, ESP32 reads FAT VFS
2. **Tab UI on TFT** — FW list, Tools, Info tabs with 3-button navigation
3. **Auto-detect target** — SWD IDCODE routing: M3->F1, M4->F4
4. **RDP auto-erase** — detects STM32 read protection, confirms with user, mass-erases
5. **Per-chunk verify** — 256B write + verify + retry (handles ~1/1000 SWD bit-bang errors)
6. **UI state restore** — saves tab+item to NVS before restart, restores on boot
7. **Serial Monitor** — real-time UART log viewer on TFT
8. **AES-128-CBC** — encrypted firmware support (optional)

## 6. Firmware Store Convention

```
/usb/                       (FAT VFS mount point)
├── ST_<name>/FW.bin        → STM32 target
├── ES_<name>/              → ESP32 target
│   ├── app.bin
│   ├── boot.bin
│   └── part.bin
└── history.txt             → flash history (disabled in v2.0)
```

## 7. Flash Partition Layout

| Name | Type | Offset | Size |
|------|------|--------|------|
| nvs | data/nvs | 0x9000 | 20KB |
| otadata | data/ota | 0xE000 | 8KB |
| app0 | app/ota_0 | 0x10000 | 3MB |
| fatfs | data/fat | 0x310000 | ~12.9MB |

## 8. Development Requirements

- ESP-IDF v5.1.6+
- Arduino component (`espressif/arduino-esp32`)
- Target: `esp32s3`
- Build: `idf.py build`

## 9. Version History

| Version | Date | Changes |
|---------|------|---------|
| v1.x | 2025 | OLED SH1106 + SD card + ESP32-C3 |
| v2.0 | 2026-03 | TFT ST7735 + USB drive + ESP32-S3 |

## 10. Unresolved Questions

- History tab + Desc tab disabled in v2.0 — re-enable later?
- `metadata_parser/` not yet moved into `modules/` — belongs to storage layer?
- `firmware_types.h` at root — move to shared types module?
