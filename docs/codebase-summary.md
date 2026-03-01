# Codebase Summary — ESP MultiFlasher

**Version:** 1.3.0
**Updated:** 2026-03-01
**Total source:** ~6700 lines (main/) + components + FlashPorter PC tool

---

## Directory Structure

```
ESP_MUL/Muti/
├── main/                           # ESP32-C3 firmware (6716 lines)
│   ├── main.cpp                    # Entry point, 5-tab UI orchestration (535L)
│   ├── firmware_types.h            # Shared data structures
│   ├── app_actions/                # Action dispatcher, UI callbacks (761L)
│   ├── flasher/
│   │   ├── flasher_common.h/.cpp   # Shared pins, IDCODE routing (86L)
│   │   ├── flasher_esp.h/.cpp      # ESP32 UART flasher (794L)
│   │   ├── flasher_esp_crypto.*    # AES decrypt for ESP segments (115L)
│   │   ├── flasher_swd_stm32f4.*  # STM32F4 SWD engine (1226L)
│   │   └── flasher_swd_stm32f1.*  # STM32F1 SWD engine (808L)
│   ├── sd_card/                    # SD card + firmware metadata + history (368L)
│   ├── net_server/                 # NetFlash HTTP API + mDNS (316L)
│   ├── ui_state/                   # Tab/item state persistence (172L)
│   ├── wifi_config/                # WiFi captive portal (299L)
│   ├── sync_engine/                # Firmware sync from cloud (371L)
│   ├── ota_downloader/             # HTTP download + AES decrypt (436L)
│   ├── metadata_parser/            # JSON index.txt parser (116L)
│   ├── file_utils/                 # File system utilities (313L)
│   └── oled/                       # Legacy menu wrapper (superseded by OledUI)
├── components/
│   ├── OledUI/                     # Custom tab UI library (tabs, menu, dialog, progress)
│   ├── Adafruit_DAP/               # SWD protocol (modified for ESP32-C3)
│   │   ├── Adafruit_DAP.h/.cpp     # Core DAP driver
│   │   ├── Adafruit_DAP_STM32.cpp  # F4 FPEC flash control
│   │   └── Adafruit_DAP_STM32F1.*  # F1 FPEC flash control
│   ├── Adafruit_SH110X/            # SH1106G OLED driver
│   ├── Adafruit_GFX/               # Graphics library
│   ├── Adafruit_BusIO/             # I2C/SPI abstraction
│   └── WiFiManager/                # WiFi captive portal
├── toolAddFirmware/
│   └── FlashPorter_Public/         # PC tool (Python/Tkinter)
│       ├── main.pyw                # GUI entry point
│       └── modules/
│           ├── firmware_lib.py     # Firmware library management
│           ├── crypto.py           # AES-128-CBC encryption
│           ├── net_flash.py        # NetFlash HTTP client
│           ├── git_sync.py         # Git clone/pull sync
│           ├── sd_card.py          # SD card export
│           ├── auth.py             # GitHub API auth
│           ├── oled_preview.py     # OLED mockup preview
│           ├── theme.py            # Tkinter theming
│           └── utils.py            # MD5, file ops
├── docs/                           # Documentation
├── CMakeLists.txt                  # Root build file (target: esp32c3)
├── sdkconfig                       # ESP-IDF configuration
└── README.md
```

---

## Core Modules

### main.cpp — UI + Orchestration (535L)

5-tab OledUI interface with callback-based action dispatch.

**Tabs:** FW(0), Tools(1), Desc(2), Hist(3), Info(4)

**Boot sequence:**
1. OLED init (SH1106G, I2C 0x3C, SDA=GPIO8, SCL=GPIO9)
2. SD mount (SPI, CS=GPIO7)
3. Load metadata (`sd_load_metadata()`)
4. Load history (`sd_history_load()`)
5. Restore UI state (`ui_state_load()`)
6. Init OledUI tabs + register callbacks
7. NetFlash auto-start if enabled

**Buttons:** UP=GPIO21, DOWN=GPIO10, OK=GPIO20

### app_actions — Action Dispatcher (761L)

Callback-based UI abstraction. 5 callbacks provided by main.cpp:
- `on_progress(text, percent)` — progress bar
- `on_confirm(title, msg)` — yes/no dialog
- `on_message(title, msg)` — info display
- `on_spinner(text, frame)` — loading animation
- `on_monitor_draw(buf, idx, activity)` — monitor screen

**Key functions:**
- `action_flash_firmware(fw_id)` — detect target type → route to ESP/STM32 engine
- `action_erase_chip_uart()` — ESP32 chip erase
- `action_erase_chip_swd()` — STM32 RDP disable + mass erase
- `swd_rdp_disable_with_retry(is_f1)` — 3x retry helper

**IDCODE routing:** metadata `device_type` field determines engine:
- "STM32F1xx" or IDCODE 0x1BA01477 (Cortex-M3) → F1 engine
- "STM32F4xx" or IDCODE 0x2BA01477 (Cortex-M4) → F4 engine
- Others → ESP32 UART engine

### flasher_common — Shared Definitions (86L)

**Dual-purpose GPIO pins:**
| GPIO | ESP32 UART Mode | STM32 SWD Mode |
|------|----------------|----------------|
| 0 | UART TX | SWDIO |
| 1 | UART RX | (free) |
| 2 | Reset trigger | (free) |
| 3 | Boot trigger | SWCLK |

**Functions:** `swd_probe_idcode()` — read IDCODE for M3/M4 detection

### flasher_esp — ESP32 UART Engine (794L)

Uses `espressif/esp-serial-flasher` library.

**API:**
- `flasher_begin_session(fw_id)` — flash complete firmware (boot+part+app)
- `flasher_write_segment(path, offset, md5)` — flash single binary
- `flasher_write_segment_encrypted(path, offset, key, iv)` — flash encrypted
- `flasher_chip_erase()` — erase target chip

**Boot combo:** 12 combinations (4 GPIO × 3 timing) brute-forced until connection.

**Flash addresses:** Bootloader=0x1000, Partition=0x8000, App=0x10000

### flasher_swd_stm32f4 — F4 SWD Engine (1226L)

**API:**
- `flasher_swd_stm32f4_detect_rdp(rdp_level)` — check RDP 0/1/2
- `flasher_swd_stm32f4_rdp_disable_trigger()` — blind write OPTCR
- `flasher_swd_stm32f4_flash_firmware(path, cb)` — full flash + verify

**Strategy:** sector erase → 256B chunks → on-the-fly verify → retry.
Dual RAM: try malloc(full file), fallback 32KB streaming.

**RDP disable:** blind writes (KEYR×2 + OPTKEYR×2 + OPTCR=0x0FFFAAEE), 15s wait, needs power cycle.

### flasher_swd_stm32f1 — F1 SWD Engine (808L)

**API:**
- `flasher_swd_stm32f1_detect_rdp(rdp_level)` — read FLASH_OBR directly
- `flasher_swd_stm32f1_rdp_disable_trigger()` — unlock OB + write 0xA5
- `flasher_swd_stm32f1_flash_firmware(path, cb)` — full flash + verify

**Key differences from F4:**
- Half-word programming (CSW 16-bit, replicate pattern)
- Page erase (1KB/2KB) instead of sector erase
- Peripheral registers readable under RDP1
- OB reload via SYSRESETREQ (no power cycle needed)
- erase_page order: PER → AR → STRT

**Device whitelist:** 0x412(LD), 0x410(MD), 0x414(HD), 0x418(CL), 0x420(VL-MD), 0x428(VL-HD), 0x430(XL)

### sd_card — SD Card + History (368L)

**API:**
- `sd_mount(cs_pin)` / `sd_unmount()`
- `sd_load_metadata()` — parse `/index.txt` JSON
- `sd_get_firmware_path(fw_id, out)` — lookup metadata
- `sd_get_menu_display_items(count)` / `sd_get_menu_id_items()`
- `sd_get_description(fw_id)` — lazy-load from `/FW_ID/description.txt`
- `sd_history_add(fw_id)` — append to history after successful flash
- `sd_history_load()` — build display strings from `/flash_history.txt`

**History:** `/flash_history.txt`, one fw_id/line, max 10 entries, most-recent-first display.

### net_server — NetFlash HTTP API (316L)

**Endpoints:**
- `GET /ping` → node ID, IP, uptime
- `GET /api/status` → busy, progress, status_text
- `GET /api/fw_list` → firmware list
- `POST /api/flash` → trigger flash (async, 202 Accepted)
- `POST /api/reboot` → restart device

**mDNS:** `flashporter-XXXX.local` (MAC-based ID)

Toggle via Tools tab "NetFlash: ON/OFF", persists in `/netflash.cfg`.

### ui_state — State Persistence (172L)

Saves/restores current tab + selected item across restarts.
File: `/config/ui_state.txt`. One-time restore (deleted after use).

### wifi_config — WiFi Portal (299L)

WiFiManager captive portal. Config from SD: `/config/url.txt`, AES key/IV from NVS.

### sync_engine — Cloud Sync (371L)

Download remote `index.txt` → compare → download changed `.enc` files → decrypt → save.

### ota_downloader — HTTP + AES (436L)

Streaming HTTP download with real-time AES-128-CBC decryption (mbedtls).

---

## Components

| Component | Purpose | Modified? |
|-----------|---------|-----------|
| OledUI | Custom tab UI (tabs, menu, dialog, progress, spinner) | Custom |
| Adafruit_DAP | SWD bit-bang protocol | Heavy (ESP32-C3 port, F1 support) |
| Adafruit_SH110X | SH1106G OLED driver | No |
| Adafruit_GFX | Graphics primitives | No |
| Adafruit_BusIO | I2C/SPI abstraction | No |
| WiFiManager | WiFi captive portal | No |
| Adafruit_SSD1306 | (unused, legacy) | — |

**Adafruit_DAP modifications:**
- `noInterrupts()` → `portENTER_CRITICAL()` (FreeRTOS compatible)
- LED_BUILTIN disabled (conflicts I2C SDA on ESP32-C3)
- PIN_INPUT_ENABLE added for ESP32
- nRESET=-1 null guards
- `dap_read_block`/`dap_write_block` return false on error
- `Serial.println` → `ESP_LOGE`
- Added `Adafruit_DAP_STM32F1.h/.cpp` for F1 FPEC

---

## FlashPorter PC Tool

Python/Tkinter GUI at `toolAddFirmware/FlashPorter_Public/`.

**Features:**
- Add firmware to library (metadata + bin files)
- Export to SD card (FAT32 structure)
- Encrypt AES-128-CBC + push to GitHub
- NetFlash: remote flash via HTTP to ESP32 nodes
- mDNS node discovery on LAN

**Dependencies:** `pycryptodome`, `tkinter`, `requests`

---

## Build System

**Framework:** ESP-IDF v5.1.6 + Arduino component
**Target:** ESP32-C3 (RISC-V, single-core)

**IDF dependencies** (from `idf_component.yml`):
- `espressif/esp-serial-flasher`
- `espressif/arduino-esp32`
- `bblanchon/ArduinoJson`

**Component dependencies** (from CMakeLists.txt):
`Adafruit_DAP`, `Adafruit_GFX`, `Adafruit_SH110X`, `OledUI`, `WiFiManager`,
`esp_http_client`, `mbedtls`, `nvs_flash`, `mdns`, `esp_http_server`, `esp_netif`

**Build:**
```bash
idf.py set-target esp32c3
idf.py build
idf.py -p COM3 flash monitor
```

**Compiler flags:** `-Werror=format` enabled, `-Wno-unused-parameter` for Arduino callbacks.

---

## Memory Management

System restarts after each flash/sync/erase operation to prevent leaks.
UI state persisted to SD before restart, restored on boot.

**SWD dual RAM strategy:**
- Try `malloc(full file)` → load all, close SD, flash from RAM (fast)
- Fallback `malloc(32KB)` → streaming segments, SD open during flash (large FW)

---

## Inter-Module Dependencies

```
main.cpp (UI layer)
├── OledUI (tabs, dialog, progress)
├── app_actions (callbacks) ─────────────────────┐
│   ├── flasher_esp (UART) ← esp-serial-flasher  │
│   ├── flasher_swd_stm32f4 (SWD) ← Adafruit_DAP│
│   ├── flasher_swd_stm32f1 (SWD) ← Adafruit_DAP│
│   └── flasher_common (IDCODE routing)           │
├── sd_card ← ArduinoJson, SD                     │
├── ui_state ← sd_card                            │
├── net_server ← esp_http_server, mdns            │
├── wifi_config ← WiFiManager                     │
├── sync_engine ← ota_downloader, metadata_parser │
└── ota_downloader ← esp_http_client, mbedtls     │
```

**Init order (setup()):** OLED → SD → metadata → history → UI state → OledUI → NetFlash
