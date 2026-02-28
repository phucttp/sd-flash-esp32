# ESP MultiFlasher

ESP32-C3 portable firmware flasher. Flash ESP32 (UART) and STM32 (SWD) targets from SD card, no PC required.

## Supported Targets

| Target | Interface | Protocol |
|--------|-----------|----------|
| ESP32 (all variants) | UART | esp-serial-flasher |
| STM32F1 (Cortex-M3) | SWD | FPEC half-word programming |
| STM32F4 (Cortex-M4) | SWD | Flash CR sector programming |

Auto-detection via SWD IDCODE: `0x1BA01477` = Cortex-M3 (F1), `0x2BA01477` = Cortex-M4 (F4).

## Hardware

**Host:** ESP32-C3 (RISC-V), ESP-IDF v5.1.6 + Arduino component

### Pin Connections

| Function | GPIO | Notes |
|----------|------|-------|
| OLED SDA | 8 | I2C, SH1106G 128x64, addr 0x3C |
| OLED SCL | 9 | |
| SD Card CS | 7 | SPI, FAT32 |
| Button UP | 21 | Active low |
| Button DOWN | 10 | Active low |
| Button OK | 20 | Active low |

### Target Wiring — ESP32 (UART)

| Host GPIO | Target Pin | Function |
|-----------|------------|----------|
| 0 (TX) | RXD0 | Firmware data |
| 1 (RX) | TXD0 | Response |
| 2 | EN/RESET | Reset control |
| 3 | GPIO0/BOOT | Boot mode |

### Target Wiring — STM32 (SWD)

| Host GPIO | Target Pin | Function |
|-----------|------------|----------|
| 0 | SWDIO | SWD data |
| 3 | SWCLK | SWD clock |
| 3.3V | VCC | Power target from 3.3V only |
| GND | GND | Common ground |

> **Warning:** Never power STM32 from 5V — causes SWD level mismatch and potential GPIO damage.

## Features

- **OLED Tab UI:** 5 tabs — FW, Tools, Desc, History, Info
- **SD Card firmware library:** JSON metadata (`index.txt`), auto-discovery
- **WiFi Sync:** Download encrypted firmware from cloud, AES-128-CBC
- **NetFlash:** Remote flash via HTTP API over WiFi
- **RDP auto-erase:** Detects STM32 read protection, confirms with user, auto-erases before flashing
- **Serial Monitor:** Real-time UART log viewer on OLED
- **Flash History:** Tracks last 10 flash operations

## SD Card Structure

```
SD_ROOT/
├── index.txt           # Firmware metadata (JSON)
├── config/
│   ├── url.txt         # Remote server URL
│   ├── aes_key.txt     # AES-128 key (16 bytes)
│   └── aes_iv.txt      # AES-128 IV (16 bytes)
└── FW_ID/
    ├── FW.bin          # Main firmware (STM32) or app.bin (ESP32)
    ├── boot.bin        # Bootloader (ESP32 only)
    └── part.bin        # Partition table (ESP32 only)
```

## Build

Requires ESP-IDF v5.1.6+.

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p COM3 flash monitor
```

## FlashPorter (PC Tool)

Python/Tkinter tool to manage firmware library and prepare SD cards.

```bash
cd toolAddFirmware/FlashPorter_Public
pip install -r requirements.txt
python main.pyw
```

Features: add firmware, export to SD, encrypt + push to cloud, NetFlash remote flashing.

## Dependencies

- `espressif/esp-serial-flasher` — UART flash protocol
- `espressif/arduino-esp32` — Arduino framework on ESP-IDF
- `bblanchon/ArduinoJson` — JSON parsing
- `Adafruit_SH110X` / `Adafruit_GFX` — OLED driver
- `Adafruit_DAP` (modified) — SWD protocol for STM32
- `WiFiManager` — WiFi captive portal
- `mbedtls` — AES encryption

## License

MIT License — TTP27 (2025-2026)
