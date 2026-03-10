# Codebase Summary — CHIVI FlashPorter TFT

> Last updated: 260307

## Project Root

```
CHIVI-TFT/
├── CMakeLists.txt              # Top-level: target=esp32s3, exclude OLED components
├── partitions.csv              # 4 partitions: nvs, otadata, app0(3MB), fatfs(~13MB)
├── sdkconfig / sdkconfig.defaults
├── components/                 # ESP-IDF components (libraries)
├── main/                       # Application source
├── toolAddFirmware/            # PC tool (Python/Tkinter)
├── image/                      # Assets
├── plans/                      # Planning docs
└── docs/                       # Project documentation
```

## Components (Libraries)

| Component | Purpose | Notes |
|-----------|---------|-------|
| Adafruit_DAP | SWD protocol (CMSIS-DAP bit-bang) | Modified: STM32F1 added, ESP32 compat fixes |
| Adafruit_ST7735 | TFT display driver | ST7735/ST7789/ST7796S |
| Adafruit_GFX | Graphics primitives | Base class for display |
| Adafruit_BusIO | I2C/SPI abstraction | |
| TftUI | Tab-based TFT UI framework | Custom component |
| esp_tinyusb | USB MSC + CDC | Internal FAT drive + debug COM |
| WiFiManager | WiFi captive portal | Present but unused in v2.0 offline mode |
| Adafruit_SH110X | OLED driver (legacy) | Excluded via EXCLUDE_COMPONENTS |
| Adafruit_SSD1306 | OLED driver (legacy) | Excluded via EXCLUDE_COMPONENTS |
| OledUI | OLED UI framework (legacy) | Excluded via EXCLUDE_COMPONENTS |

## Application Source (`main/`)

```
main/
├── main.cpp                    # Entry point, tab UI setup, callbacks
├── pin_config.h                # All GPIO assignments (centralized)
├── firmware_types.h            # firmware_metadata_t struct + FirmwareMap typedef
├── CMakeLists.txt              # idf_component_register(SRCS...)
├── metadata_parser/            # JSON metadata parser (index.txt)
│   ├── metadata_parser.cpp
│   └── metadata_parser.h
└── modules/
    ├── prog/                   # Flasher engines
    │   ├── prog_common.cpp/h       # Shared: pin init, IDCODE probe, mode enum
    │   ├── esp32/
    │   │   ├── prog_esp32.cpp/h        # ESP32 UART flasher (esp-serial-flasher)
    │   │   └── prog_esp32_crypto.cpp/h # AES-128-CBC decrypt for encrypted FW
    │   └── stm32/
    │       ├── prog_stm32_f4.cpp/h     # STM32F4 SWD (sector erase, word program)
    │       └── prog_stm32_f1.cpp/h     # STM32F1 SWD (page erase, half-word program)
    ├── storage/                # Data storage
    │   ├── sd_card/
    │   │   ├── sd_card.cpp/h       # FW store: scan /usb dirs, build metadata, history
    │   └── usb_drive/
    │       └── usb_drive.cpp/h     # TinyUSB MSC+CDC: mount FAT, lock/unlock
    ├── ui/                     # User interface logic
    │   ├── app_actions/
    │   │   ├── app_actions.cpp/h   # High-level actions: flash, erase, monitor, scan
    │   └── ui_state/
    │       └── ui_state.cpp/h      # Save/restore tab+item across reboots (NVS)
    └── utils/                  # Utilities
        ├── file_utils/
        │   └── file_utils.cpp/h    # File I/O helpers
        └── flash_log/
            └── flash_log.cpp/h     # Flash operation logging
```

## Key Data Flow

1. **Boot**: `app_main()` → `initArduino()` → `setup()` → `loop()`
2. **Storage init**: `usb_drive_init()` → mount FAT → `sd_load_metadata()` → scan `/usb/ST_*`, `/usb/ES_*`
3. **UI loop**: `runTabsUI()` → `ui.tabsRun()` (blocking) → `handleTabSelection(tab, item)`
4. **Flash action**: `action_flash_firmware(fw_id)` → lookup metadata → detect target type → route to engine
5. **SWD routing**: `swd_probe_idcode()` → 0x1BA01477=F1, 0x2BA01477=F4
6. **USB mode switching**: `usb_drive_lock()` = ESP reads FAT, `usb_drive_unlock()` = PC sees drive

## File Counts

| Category | Files | Lines (approx) |
|----------|-------|-----------------|
| prog/ | 10 (.cpp+.h) | ~2500 |
| storage/ | 4 | ~600 |
| ui/ | 4 | ~700 |
| utils/ | 4 | ~300 |
| root (main.cpp etc) | 4 | ~550 |
| **Total main/** | **26** | **~4650** |

## Build Dependencies (CMakeLists.txt REQUIRES)

```
espressif__arduino-esp32  Adafruit_DAP  Adafruit_GFX  Adafruit_ST7735
Adafruit_BusIO  TftUI  wear_levelling  fatfs  nvs_flash  driver  esp_tinyusb
```
