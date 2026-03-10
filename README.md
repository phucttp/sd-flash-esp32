# CHIVI FlashPorter TFT

Standalone firmware flasher on ESP32-S3. Programs ESP32 (UART) and STM32 (SWD) targets from internal USB drive — no PC required during flashing.

Firmware files are copied via USB drag-and-drop, then flashed autonomously using a 3-button TFT interface.

## Supported Targets

| Target | Interface | Protocol | Auto-detect |
|--------|-----------|----------|-------------|
| ESP32 (all variants) | UART | esp-serial-flasher | N/A |
| STM32F1 (Cortex-M3) | SWD | FPEC half-word | IDCODE 0x1BA01477 |
| STM32F4 (Cortex-M4) | SWD | Flash CR sector | IDCODE 0x2BA01477 |

## Hardware

| Component | Spec |
|-----------|------|
| MCU | ESP32-S3 (Xtensa dual-core, 240MHz) |
| Display | ST7735 TFT 160x128 (SPI, landscape) |
| Storage | Internal FAT ~12.9MB via TinyUSB MSC |
| Navigation | 3 buttons (UP/DOWN/OK) |
| USB | MSC (drive) + CDC (debug COM) |

### Pin Map

| Function | GPIO | Notes |
|----------|------|-------|
| TFT MOSI | 34 | Hardware SPI |
| TFT SCLK | 33 | |
| TFT RST/DC/BL | 35/36/37 | BL via MOSFET |
| BTN UP/DOWN/OK | 4/1/2 | Active low |
| FLASH_PIN_0 | 13 | ESP TX / SWD SWDIO |
| FLASH_PIN_1 | 14 | ESP RX |
| FLASH_PIN_2 | 12 | ESP RESET trigger |
| FLASH_PIN_3 | 11 | ESP BOOT / SWD SWCLK |

> **Warning:** Power STM32 target from 3.3V only. 5V causes SWD level mismatch.

## Features

- **USB Drive** — PC copies firmware via drag-and-drop, ESP32 reads FAT VFS
- **TFT Tab UI** — FW list, Tools, Info with color display
- **Auto-detect STM32** — IDCODE routing: Cortex-M3 -> F1, Cortex-M4 -> F4
- **RDP auto-erase** — detect read protection, confirm, mass-erase before flash
- **Per-chunk verify** — 256B write + readback + retry (handles SWD bit-bang errors)
- **Serial Monitor** — real-time UART log on TFT
- **UI state restore** — saves position across reboots
- **AES-128-CBC** — encrypted firmware support (optional)

## USB Drive Structure

```
/usb/                           (internal FAT mount)
├── ST_<name>/FW.bin            -> STM32 firmware
└── ES_<name>/                  -> ESP32 firmware
    ├── app.bin
    ├── boot.bin
    └── part.bin
```

## Project Structure

```
main/
├── main.cpp                    Entry point + UI callbacks
├── pin_config.h                GPIO definitions
├── firmware_types.h            Shared types
└── modules/
    ├── prog/                   Flasher engines
    │   ├── prog_common.*           Shared (IDCODE probe, pin init)
    │   ├── esp32/                  ESP32 UART engine
    │   └── stm32/                  STM32F1/F4 SWD engines
    ├── storage/                USB drive + firmware store
    ├── ui/                     App actions + UI state
    └── utils/                  File utils + flash log
```

## Build

Requires ESP-IDF v5.1.6+.

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

## Dependencies

| Component | Purpose |
|-----------|---------|
| `espressif/arduino-esp32` | Arduino framework on ESP-IDF |
| `espressif/esp-serial-flasher` | UART flash protocol |
| `Adafruit_DAP` (modified) | SWD protocol for STM32 |
| `Adafruit_ST7735` / `Adafruit_GFX` | TFT display driver |
| `TftUI` | Tab-based UI framework |
| `esp_tinyusb` | USB MSC + CDC |
| `mbedtls` | AES encryption (ESP-IDF built-in) |

## FlashPorter (PC Tool)

Python/Tkinter tool for firmware library management.

```bash
cd toolAddFirmware/FlashPorter_Public
pip install -r requirements.txt
python main.pyw
```

## Documentation

Detailed docs in [`docs/`](docs/):
- [Project Overview & PDR](docs/project-overview-pdr.md)
- [Codebase Summary](docs/codebase-summary.md)
- [Code Standards](docs/code-standards.md)
- [System Architecture](docs/system-architecture.md)

## License

MIT License — TTP27 (2025-2026)
