# ESP32 Multi-Flasher - Project Overview & PDR

## Project Purpose

**ESP32 Multi-Flasher (FlashPorter)** transforms an ESP32-C3 Host device into a portable **multi-MCU firmware flasher** operating without PC. Supports:
- **ESP32 targets** via UART (esp-serial-flasher)
- **STM32 targets** via SWD (Adafruit_DAP bit-bang)

Enables in-field firmware updates with OLED tab-based menu control, online WiFi sync, and AES-encrypted firmware delivery.

## Target Hardware

**Primary MCU:** ESP32-C3 (RISC-V, single-core, 400KB SRAM)
**Framework:** ESP-IDF v5.1.6 with Arduino component
**Display:** SH1106G OLED 128x64 via I2C

## Key Features

### Offline Mode (SD Card Based)
- **Tabs UI:** Interactive SH1106G OLED display (128x64) with 3-button control, 5 tabs (FW, Tools, Desc, Hist, Info)
- **Firmware Library:** JSON-indexed firmware catalog on SD card (index.txt)
- **ESP32 UART Flash:** Uses `espressif/esp-serial-flasher` for high-speed UART flashing
- **STM32 SWD Flash:** Uses Adafruit_DAP bit-bang SWD with on-the-fly verify + retry
- **Auto-detect Target:** Determines ESP32 vs STM32 from metadata `device_type` field
- **MD5 Verification:** Optional firmware integrity checking post-flash (ESP32 targets)
- **Monitor Mode:** UART log viewer for target device after flashing
- **Chip Erase:** Wipe target flash (ESP32 UART or STM32 SWD)
- **RDP Disable:** Unlock STM32 read protection via SWD blind writes + mass erase

### Online Mode (WiFi Sync)
- **Auto-Sync:** Download firmware from GitHub/remote server
- **AES-128-CBC Decryption:** Secure firmware transmission (encrypted .enc files)
- **Smart Sync:** Compare local vs remote index, download only changed files
- **WiFi Portal:** Configure WiFi and server URL via WiFiManager
- **Force Clean:** Long-press option to delete all local firmware and re-sync

### Hardware Control
- **Dual-Purpose GPIO Pins:** Same 4 GPIOs serve ESP32 UART mode or STM32 SWD mode
- **Target Boot Control:** GPIO-based EN/BOOT manipulation for flash mode entry (ESP32)
- **Progress Display:** Real-time flash progress on OLED
- **Cancel Mechanism:** UP+DOWN combo to cancel any running action
- **Debounced Buttons:** Reliable user input (UP/DOWN/OK)

## Hardware Requirements

### Host ESP32-C3 (Flasher)
```
MCU:       ESP32-C3 (RISC-V, single-core)
OLED:      SH1106G I2C @ 0x3C (128x64)
           SDA → GPIO 8
           SCL → GPIO 9
SD Card:   SPI Interface
           CS → GPIO 7
Buttons:   BTN_UP → GPIO 21
           BTN_DOWN → GPIO 10
           BTN_OK → GPIO 20
```

### Target: ESP32 (UART Mode)
```
Host GPIO 0 (TX)  → Target RXD0     (UART data)
Host GPIO 1 (RX)  → Target TXD0     (UART response)
Host GPIO 2       → Target EN/RESET  (Reset trigger)
Host GPIO 3       → Target GPIO0/BOOT (Boot mode trigger)
```

### Target: STM32 (SWD Mode)
```
Host GPIO 0 (SWDIO)  → Target SWDIO   (SWD data, bidirectional)
Host GPIO 3 (SWCLK)  → Target SWCLK   (SWD clock)
nRESET: Not connected (-1), use physical reset button on target
Power: Use ESP32-C3 3.3V pin — NEVER use 5V pin (voltage mismatch!)
```

**Pin Sharing (flasher_common.h):**
| GPIO | ESP32 UART Mode | STM32 SWD Mode |
|------|----------------|----------------|
| GPIO 0 | UART TX | SWDIO |
| GPIO 1 | UART RX | (free) |
| GPIO 2 | Reset trigger | (free) |
| GPIO 3 | Boot trigger | SWCLK |

## Product Requirements

### Functional Requirements

**FR1: Offline Flashing (ESP32 UART)**
- Must read firmware catalog from SD card index.txt (JSON format)
- Must display firmware list in scrollable OLED menu
- Must flash bootloader (0x1000), partition table (0x8000), app (0x10000)
- Must verify flash using MD5 if provided
- Must display progress percentage during flash

**FR2: Offline Flashing (STM32 SWD)**
- Must auto-detect STM32 target from `device_type` field in metadata
- Must detect RDP level before attempting flash
- Must refuse flash if RDP Level 1/2 (direct user to erase first)
- Must flash firmware with on-the-fly verify (256B chunks)
- Must retry failed chunks (3x per chunk, 3 full re-flash attempts)
- Must support firmware larger than free RAM via 32KB streaming segments

**FR3: STM32 RDP Management**
- Must detect RDP Level 0/1/2 via SWD
- Must disable RDP Level 1 via blind writes (KEYR + OPTKEYR + OPTCR)
- Must wait for mass erase (15s) with IDCODE keepalive
- Must prompt user for power cycle after RDP disable
- Must verify RDP=Level 0 after power cycle
- Must perform rescue erase if flash has leftover data

**FR4: Online Sync**
- Must connect to WiFi using saved credentials or captive portal
- Must download encrypted firmware from configurable URL
- Must decrypt AES-128-CBC encrypted files
- Must sync local index with remote index
- Must delete obsolete firmware when syncing

**FR5: Monitor Mode**
- Must capture UART output from target device at 115200 baud
- Must display target logs on OLED (7 lines, 21 chars/line)
- Must allow exit via OK button or UP+DOWN combo

**FR6: System Commands**
- Must support chip erase (ESP32 UART and STM32 SWD)
- Must restart host after operations to free RAM
- Must handle SD card mount failures gracefully
- Must support cancel via UP+DOWN combo during any action

### Non-Functional Requirements

**NFR1: Performance**
- UART flash speed: High-speed serial flasher (default baudrate)
- SWD flash speed: ~256B per programBlock cycle (~3 min for 72 chunks)
- Monitor mode: 115200 baud
- SD read buffer: 4096 bytes (ESP UART), 32KB segments (SWD streaming)
- Menu response: <50ms debounce

**NFR2: Reliability**
- Must validate SD card presence on boot
- Must halt on OLED init failure
- Must handle empty menu gracefully
- Must verify SWD flash integrity on-the-fly (memcmp after each chunk)
- Must retry SWD operations (chunk-level + full re-flash)
- Must restart system after each operation (memory management)

**NFR3: Usability**
- Clear OLED status messages with structured progress ([1/5]...[5/5])
- Tabs-based navigation: UP+DOWN=switch tab, UP/DOWN=scroll, OK=select
- Cancel mechanism: UP+DOWN during action → confirm dialog
- Auto-exit after operation completion

**NFR4: Security**
- AES-128-CBC encryption for OTA downloads
- Configurable encryption keys stored on SD
- MD5 hash verification support (ESP32 targets)

## User Stories

**US1: Field Technician — Quick Flash (ESP32)**
As field technician, need flash pre-loaded firmware to ESP32 device without laptop. Power on flasher, select firmware from FW tab, press OK, wait for completion.

**US2: Field Technician — Quick Flash (STM32)**
As field technician, need flash pre-loaded firmware to STM32 device. Power on flasher, select STM32 firmware from FW tab, press OK. Flasher auto-detects STM32 target and uses SWD engine.

**US3: Developer — Remote Update**
As developer, need update firmware library remotely. Connect flasher to WiFi, select Sync from Tools tab, wait for download, then flash updated firmware offline.

**US4: Production Line — Chip Erase (ESP32)**
As production worker, need erase used ESP32 for recycling. Select "Erase Chip (ESP)" from Tools tab, confirm, wait for chip wipe.

**US5: Production Line — STM32 RDP Unlock**
As production worker, need unlock a read-protected STM32. Select "Erase Chip (SWD)" from Tools tab. Flasher detects RDP level, triggers mass erase, prompts for power cycle, verifies unlock.

**US6: Support Engineer — Debug Logs**
As support engineer, need view device logs after flashing. Select "Monitor/Test" from Tools tab, read serial output, press OK to exit.

**US7: Admin — Update Firmware Catalog**
As admin, need update multiple firmware versions. Use FlashPorter PC tool to prepare SD card with new index.txt and firmware binaries (both ESP32 .bin and STM32 .bin).

## Architecture

### Software Layers
```
┌─────────────────────────────────────────────────┐
│  main.cpp — UI layer (OledUI tabs + callbacks)  │
├─────────────────────────────────────────────────┤
│  app_actions — Business logic (action_* API)    │
│  Callback-based UI abstraction (5 callbacks)    │
├──────────────────┬──────────────────────────────┤
│  flasher_esp     │  flasher_swd                 │
│  (UART engine)   │  (SWD engine)                │
│  esp-serial-     │  Adafruit_DAP STM32          │
│  flasher lib     │  bit-bang SWD + verify        │
├──────────────────┴──────────────────────────────┤
│  flasher_common — Shared pins, constants, mode  │
├─────────────────────────────────────────────────┤
│  sd_card / wifi_config / sync_engine / OledUI   │
└─────────────────────────────────────────────────┘
```

### Callback-based UI Decoupling
UI layer (main.cpp) provides 5 callbacks via `app_actions_config_t`:
- `on_progress(text, percent)` — Progress bar updates
- `on_confirm(title, msg)` — Yes/No dialog
- `on_message(title, msg)` — Info display
- `on_spinner(text, frame)` — Loading animation
- `on_monitor_draw(log_buf, idx, activity)` — Monitor screen

App actions call these callbacks without knowing the display hardware.

### STM32 SWD Flash Strategy
```
┌─ Try malloc(full file) ─┐
│   OK → Full-RAM mode    │   FAIL → malloc(32KB) → Streaming mode
│   Read all, close SD    │         Keep file open during flash
│   Flash from RAM        │         Read 32KB segments, flash, repeat
└─────────────────────────┘
                    ↓
         Erase → Program 256B chunks → Verify each → Retry on mismatch
                    ↓
         3 chunk retries + 3 full re-flash attempts
```

## Technical Constraints

**TC1: SD Card Limitations**
- FAT filesystem: 8.3 filename format
- JSON index file must be named index.txt at root
- Firmware directories use short folder names

**TC2: Memory Constraints**
- System restart after each operation to prevent memory leaks
- SWD flash: dual strategy — full-RAM (fast) or 32KB streaming (large FW)
- JSON parsing limited by available RAM

**TC3: UART Protocol (ESP32 Targets)**
- Fixed GPIO mapping for flash interface (cannot reassign at runtime)
- Target must support ESP serial flasher protocol
- Monitor mode assumes 115200 baud serial output

**TC4: SWD Protocol (STM32 Targets)**
- Bit-bang SWD on GPIO0 (SWDIO) + GPIO3 (SWCLK) — no hardware SWD peripheral
- GPIO0 chosen to avoid FSPIQ conflict (GPIO2 is FSPI default MISO)
- ~1/1000 word write failure rate → on-the-fly verify mandatory
- F4: RDP Level 1 blocks all AHB-AP reads; only blind writes possible
- F4: RDP disable requires physical power cycle (no OBL_LAUNCH)
- F1: Peripheral registers readable under RDP1; OB reload via SYSRESETREQ (no power cycle)
- STM32 must be powered from 3.3V pin (5V causes voltage mismatch)

**TC5: WiFi Dependencies**
- Requires 2.4GHz WiFi access point
- HTTP(S) download only (no FTP/other protocols)
- AES key/IV must be 16 bytes exactly

## Implementation Status

**Current Version:** 1.3.0
**Updated:** 2026-03-01
**Production Status:** Multi-MCU Flash Working (ESP32 + STM32F1 + STM32F4)

### Implemented ✅
- ESP32 UART flashing (esp-serial-flasher) + brute-force boot combos
- **STM32F4 SWD flash** (Adafruit_DAP, sector erase, on-the-fly verify + retry)
- **STM32F1 SWD flash** (FPEC half-word, page erase, device whitelist)
- **IDCODE auto-detect:** 0x1BA01477→F1, 0x2BA01477→F4
- **RDP detect/disable** for both F1 and F4 (blind writes + mass erase)
- **RDP auto-erase with retry** (3x retry + confirm dialog before flash)
- Streaming flash for large firmware (32KB segments)
- SD card management, JSON parsing, flash history (last 10)
- OLED SH1106G 5-tab UI with OledUI library (FW, Tools, Desc, Hist, Info)
- Callback-based UI abstraction (app_actions layer)
- **UI state persistence** (tab + item restored after restart)
- WiFi configuration portal
- OTA downloader with AES-128-CBC decryption
- Sync engine for index comparison
- **NetFlash HTTP API** (remote flash via WiFi, mDNS discovery)
- Monitor mode UART capture
- Chip erase (ESP32 UART and STM32 SWD)

### Pending
- SD card logging for field debugging
- Error recovery resume (mid-flash checkpoint)
- RDP auto-lock after flash (production mode)
- Production testing with diverse STM32 boards

### Source Files (main/, ~6700 lines)
- `main.cpp` — 5-tab UI + callback wiring (535L)
- `app_actions/` — Action dispatch + IDCODE routing (761L)
- `flasher/flasher_common.*` — Shared pins, `swd_probe_idcode()` (86L)
- `flasher/flasher_esp.*` — ESP32 UART engine (794L)
- `flasher/flasher_esp_crypto.*` — AES decrypt for ESP segments (115L)
- `flasher/flasher_swd_stm32f4.*` — F4 SWD engine (1226L)
- `flasher/flasher_swd_stm32f1.*` — F1 SWD engine (808L)
- `sd_card/` — SD card + metadata + history (368L)
- `net_server/` — NetFlash HTTP API + mDNS (316L)
- `ui_state/` — Tab/item state persistence (172L)
- `wifi_config/` — WiFi captive portal (299L)
- `sync_engine/` — Cloud sync orchestrator (371L)
- `ota_downloader/` — HTTP download + AES (436L)
- `metadata_parser/` — JSON parser (116L)
- `file_utils/` — File utilities (313L)

**PC Tool (toolAddFirmware/FlashPorter_Public/):**
- Modular Python/Tkinter GUI (main.pyw + 9 modules)
- Features: firmware library, SD export, AES encrypt, cloud push, NetFlash remote flash

### Build Configuration
- Target: esp32c3 (set via CMakeLists.txt)
- IDF Version: v5.1.6+
- Arduino Component: Integrated via idf_component.yml
- Build System: CMake + Ninja
- `-Werror=format` enabled — format mismatches are fatal

### Deployment
- Flash via `idf.py flash` (USB Serial JTAG on ESP32-C3)
- SD card preparation via FlashPorter tool: `toolAddFirmware/dist/FlashPorter.exe`
- FlashPorter source: `toolAddFirmware/FlashPorter.py` (Python 3 + pycryptodome)

## Dependencies

**Core IDF Components:**
- ESP-IDF v5.1.6+
- Arduino-ESP32 component
- espressif/esp-serial-flasher
- FreeRTOS task management

**External Libraries:**
- ArduinoJson (bblanchon) for JSON parsing
- Adafruit_SH110X for OLED SH1106G display
- Adafruit_GFX for graphics primitives
- **Adafruit_DAP** for STM32 SWD debug access port (modified for ESP32-C3)
- WiFiManager for WiFi provisioning
- OledUI — Custom tabs/menu/dialog/progress library

**Hardware Dependencies:**
- SH1106G OLED 128x64 via I2C
- MicroSD card via SPI
- 3x push buttons (pull-up configuration)

## Multi-MCU Support Matrix

| Target MCU | Interface | Engine | Status |
|------------|-----------|--------|--------|
| ESP32 | UART | esp-serial-flasher | Done (v1.0) |
| ESP32-C3 | UART | esp-serial-flasher | Done (v1.0) |
| ESP32-S3 | UART | esp-serial-flasher | Done (v1.0) |
| STM32F4xx | SWD | Adafruit_DAP | Done (v1.2) |
| STM32F1xx | SWD | Adafruit_DAP_STM32F1 | Done (v1.3) |

## Success Metrics

**M1: Flash Reliability**
- ESP32 UART: >99% successful flash operations (MD5 verified)
- STM32 SWD: >99% with on-the-fly verify + retry (typically 3/72 chunks need 1 retry)

**M2: User Experience**
- Target: <10 seconds from power-on to menu ready
- Auto-detect target: no manual MCU selection needed

**M3: Sync Efficiency**
- Target: Download only changed files (not full catalog)

**M4: System Stability**
- Target: Zero memory leaks (via restart strategy)
- SWD: Dual RAM strategy handles FW of any size within flash capacity

## Unresolved Questions

1. ~~FlashPorter tool source code repository?~~ **RESOLVED**: `toolAddFirmware/FlashPorter_Public/`
2. ~~Error recovery if flash interrupted mid-process?~~ **PARTIAL**: SWD has chunk retry + full re-flash; no mid-flash checkpoint/resume yet
3. Full support verification for ESP32-S3/ESP32 variants (GPIO mapping differences)?
4. Maximum firmware library size constraints (menu item limit, RAM for JSON parsing)?
5. ~~STM32F1xx SWD compatibility?~~ **RESOLVED**: F1 engine done (v1.3), tested on STM32F103 HD 256KB
6. Field deployment feedback (reliability metrics, failure modes)?
7. Battery-powered operation requirements (low-power mode, voltage monitoring)?
8. Production testing with diverse STM32 boards (bluepill, nucleo, custom)?
