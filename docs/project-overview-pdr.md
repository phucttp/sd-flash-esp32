# ESP32 Multi-Flasher - Project Overview & PDR

## Project Purpose

**ESP32 Multi-Flasher** transforms ESP32 Host device into portable firmware flasher operating without PC. Enables in-field firmware updates for target ESP32 devices via UART interface with OLED menu control.

## Target Hardware

**Primary MCU:** ESP32-C3 (configured in CMakeLists)
**Framework:** ESP-IDF v5.1.6 with Arduino component

## Key Features

### Offline Mode (SD Card Based)
- **Menu System:** Interactive SSD1306 OLED display (128x32) with 3-button control
- **Firmware Library:** JSON-indexed firmware catalog on SD card (index.txt)
- **Fast Flashing:** Uses `espressif/esp-serial-flasher` library for high-speed UART flashing
- **MD5 Verification:** Optional firmware integrity checking post-flash
- **Monitor Mode:** UART log viewer for target device after flashing
- **Chip Erase:** Wipe target flash completely

### Online Mode (WiFi Sync)
- **Auto-Sync:** Download firmware from GitHub/remote server
- **AES-128-CBC Decryption:** Secure firmware transmission (encrypted .enc files)
- **Smart Sync:** Compare local vs remote index, download only changed files
- **WiFi Portal:** Configure WiFi and server URL via WiFiManager
- **Force Clean:** Long-press option to delete all local firmware and re-sync

### Hardware Control
- **Target Boot Control:** GPIO-based EN/BOOT manipulation for flash mode entry
- **Progress Display:** Real-time flash progress on OLED
- **Debounced Buttons:** Reliable user input (UP/DOWN/OK)

## Hardware Requirements

### Host ESP32 (Flasher)
```
MCU:       ESP32-C3
OLED:      SSD1306 I2C @ 0x3C
           SDA → GPIO 8
           SCL → GPIO 9
SD Card:   SPI Interface
           CS → GPIO 7
Buttons:   BTN_UP → GPIO 21
           BTN_DOWN → GPIO 20
           BTN_OK → GPIO 10
```

### Target ESP32 (Device Being Flashed)
```
Connection:
Host GPIO 0 → Target RXD0 (UART data)
Host GPIO 1 → Target TXD0 (UART response)
Host GPIO 2 → Target EN/RESET
Host GPIO 3 → Target GPIO0/BOOT
```

## Product Requirements

### Functional Requirements

**FR1: Offline Flashing**
- Must read firmware catalog from SD card index.txt (JSON format)
- Must display firmware list in scrollable OLED menu
- Must flash bootloader (0x1000), partition table (0x8000), app (0x10000)
- Must verify flash using MD5 if provided
- Must display progress percentage during flash

**FR2: Online Sync**
- Must connect to WiFi using saved credentials or captive portal
- Must download encrypted firmware from configurable URL
- Must decrypt AES-128-CBC encrypted files
- Must sync local index with remote index
- Must delete obsolete firmware when syncing

**FR3: Monitor Mode**
- Must capture UART output from target device
- Must display target logs via ESP_LOGI
- Must allow exit via OK button

**FR4: System Commands**
- Must support chip erase command
- Must restart host after operations to free RAM
- Must handle SD card mount failures gracefully

### Non-Functional Requirements

**NFR1: Performance**
- UART flash speed: High-speed serial flasher (default baudrate)
- Monitor mode: 115200 baud
- SD read buffer: 4096 bytes
- Menu response: <50ms debounce

**NFR2: Reliability**
- Must validate SD card presence on boot
- Must halt on OLED init failure
- Must handle empty menu gracefully
- Must verify firmware integrity via MD5
- Must restart system after each operation (memory management)

**NFR3: Usability**
- Clear OLED status messages (Booting, Flashing, Success, Error)
- 3-button navigation (UP=scroll up, DOWN=scroll down, OK=select)
- Long-press detection (3s) for force clean
- Auto-exit after operation completion

**NFR4: Security**
- AES-128-CBC encryption for OTA downloads
- Configurable encryption keys stored on SD
- MD5 hash verification support

## User Stories

**US1: Field Technician - Quick Flash**
As field technician, need flash pre-loaded firmware to device without laptop. Power on flasher, select firmware from menu, press OK, wait for completion.

**US2: Developer - Remote Update**
As developer, need update firmware library remotely. Connect flasher to WiFi, select Sync option, wait for download, then flash updated firmware offline.

**US3: Production Line - Chip Erase**
As production worker, need erase used ESP32 for recycling. Select "Erase" from menu, confirm, wait for chip wipe.

**US4: Support Engineer - Debug Logs**
As support engineer, need view device logs after flashing. Select "Monitor" mode, read serial output, press OK to exit.

**US5: Admin - Update Firmware Catalog**
As admin, need update multiple firmware versions. Use FlashPorter PC tool to prepare SD card with new index.txt and firmware binaries.

## Technical Constraints

**TC1: SD Card Limitations**
- FAT filesystem: 8.3 filename format (8 chars name + 3 chars extension)
- JSON index file must be named index.txt at root
- Firmware directories use short folder names

**TC2: Memory Constraints**
- System restart after each operation to prevent memory leaks
- 4KB buffer size for flash operations
- JSON parsing limited by available RAM

**TC3: UART Protocol**
- Fixed GPIO mapping for flash interface (cannot reassign at runtime)
- Target must support ESP serial flasher protocol
- Monitor mode assumes 115200 baud serial output

**TC4: WiFi Dependencies**
- Requires 2.4GHz WiFi access point
- HTTP(S) download only (no FTP/other protocols)
- AES key/IV must be 16 bytes exactly

## Implementation Status

**Implemented:**
- Core flashing logic with esp-serial-flasher
- SD card management and JSON parsing
- OLED menu system with button control
- WiFi configuration portal
- OTA downloader with AES decryption
- Sync engine for index comparison
- Monitor mode UART capture
- Chip erase functionality

**Pending/Unknown:**
- FlashPorter PC tool implementation status
- Production testing results
- Field deployment feedback
- Error recovery edge cases

## Success Metrics

**M1: Flash Reliability**
- Target: >99% successful flash operations
- Measurement: Flash completion + MD5 verification

**M2: User Experience**
- Target: <10 seconds from power-on to menu ready
- Measurement: Boot time log analysis

**M3: Sync Efficiency**
- Target: Download only changed files (not full catalog)
- Measurement: Network traffic analysis

**M4: System Stability**
- Target: Zero memory leaks (via restart strategy)
- Measurement: No hung system states

## Dependencies

**Core IDF Components:**
- ESP-IDF v5.1.6+
- Arduino-ESP32 component
- espressif/esp-serial-flasher
- FreeRTOS task management

**External Libraries:**
- ArduinoJson (bblanchon) for JSON parsing
- Adafruit_SSD1306 for OLED display
- Adafruit_GFX for graphics primitives
- WiFiManager for WiFi provisioning

**Hardware Dependencies:**
- SSD1306 OLED via I2C
- MicroSD card via SPI
- 3x push buttons (pull-up configuration)

## Implementation Details

**Current Version:** 1.0.0
**Release Date:** 2025-11-27
**Production Status:** MVP Ready

### Implemented Modules
**ESP32 Firmware (main/):**
- main.cpp - Application orchestrator with Arduino-style setup/loop
- sd_card/ - SD card management with JSON parsing
- flasher/ - UART serial flasher using esp-serial-flasher library
- oled/menu - SSD1306 display driver with button navigation
- wifi_config/ - WiFiManager integration for captive portal
- ota_downloader/ - HTTP client with streaming AES-128-CBC decryption
- metadata_parser/ - ArduinoJson-based firmware catalog parser
- sync_engine/ - Firmware synchronization orchestrator

**PC Tool (toolAddFirmware/):**
- FlashPorter.py - Python/Tkinter GUI for SD card preparation
- esp_encryptor.py - AES-128-CBC encryption utility
- FlashPorter.exe - Compiled Windows executable (dist/)
- Features: Add firmware, copy to SD, encrypt for GitHub, push to cloud

### Build Configuration
- Target: esp32c3 (set via CMakeLists.txt)
- IDF Version: v5.1.6+
- Arduino Component: Integrated via idf_component.yml
- Build System: CMake + Ninja

### Deployment
- Flash via idf.py flash (USB Serial JTAG on ESP32-C3)
- SD card preparation via FlashPorter tool: `toolAddFirmware/dist/FlashPorter.exe`
- FlashPorter source: `toolAddFirmware/FlashPorter.py` (Python 3 + pycryptodome)

## Unresolved Questions

1. ~~FlashPorter tool source code repository?~~ **RESOLVED**: `toolAddFirmware/FlashPorter.py`
2. Error recovery if flash interrupted mid-process (retry logic, checkpointing)?
3. Full support verification for ESP32-S3/ESP32 variants (GPIO mapping differences)?
4. Maximum firmware library size constraints (menu item limit, RAM for JSON parsing)?
5. GitHub private repository authentication method (bearer tokens, OAuth)?
6. Certificate validation for HTTPS downloads (CA bundle, certificate pinning)?
7. Field deployment feedback (reliability metrics, failure modes)?
8. Battery-powered operation requirements (low-power mode, voltage monitoring)?
9. Multi-language UI support (menu internationalization)?
10. FlashPorter uses `git push --force` - safe for multi-user workflows?
