# System Architecture — CHIVI FlashPorter TFT

> Last updated: 260307

## 1. High-Level Architecture

```
┌─────────────────────────────────────────────────────┐
│                    main.cpp                         │
│            (entry point, UI callbacks)              │
└───────┬──────────────┬──────────────┬───────────────┘
        │              │              │
   ┌────▼────┐   ┌─────▼─────┐  ┌────▼────┐
   │   UI    │   │  Storage  │  │  Prog   │
   │ Layer   │   │  Layer    │  │  Layer  │
   └────┬────┘   └─────┬─────┘  └────┬────┘
        │              │              │
   ┌────▼────┐   ┌─────▼─────┐  ┌────▼──────────────┐
   │app_act  │   │ usb_drive │  │ prog_common       │
   │ui_state │   │ sd_card   │  │ esp32/prog_esp32  │
   └─────────┘   └───────────┘  │ stm32/prog_f1,f4 │
                                └───────────────────┘
        │              │              │
   ┌────▼────┐   ┌─────▼─────┐  ┌────▼────┐
   │  TftUI  │   │ esp_tinyusb│  │Adafruit │
   │(component)│ │  fatfs    │  │  _DAP   │
   └─────────┘   └───────────┘  └─────────┘
```

## 2. Layer Responsibilities

### UI Layer (`modules/ui/`)

| Module | Role |
|--------|------|
| `app_actions` | High-level action dispatcher: flash, erase, monitor, scan. Bridges UI events to flasher engines. Receives UI callbacks (progress, confirm, message) via config struct. |
| `ui_state` | Persists current tab + selected item to NVS. Restores after reboot so user returns to same screen. |

### Storage Layer (`modules/storage/`)

| Module | Role |
|--------|------|
| `usb_drive` | Manages internal FAT partition (~12.9MB) via TinyUSB MSC. Provides VFS mount at `/usb`. Lock/unlock controls PC write access during flash operations. CDC provides debug COM port. |
| `sd_card` | Scans `/usb` for firmware directories (prefix `ST_`=STM32, `ES_`=ESP32). Builds `FirmwareMap` metadata. Provides menu item arrays for UI. |

### Prog Layer (`modules/prog/`)

| Module | Role |
|--------|------|
| `prog_common` | Shared: GPIO init/deinit, `swd_probe_idcode()` for IDCODE-based target routing, `host_system_restart()` with UI state save. |
| `esp32/prog_esp32` | ESP32 UART flasher via `esp-serial-flasher` library. Handles bootloader/partition/app multi-file flashing. |
| `esp32/prog_esp32_crypto` | AES-128-CBC streaming decryption for encrypted firmware files. |
| `stm32/prog_stm32_f4` | STM32F4 SWD engine: sector erase, word programming, RDP detect/disable with blind write + 3x retry. ~1200 lines. |
| `stm32/prog_stm32_f1` | STM32F1 SWD engine: page erase, half-word programming, RDP via FLASH_OBR. ~800 lines. |

### Utils Layer (`modules/utils/`)

| Module | Role |
|--------|------|
| `file_utils` | File I/O helpers (read, write, path operations) |
| `flash_log` | Logging flash operations to USB drive |

## 3. Boot Sequence

```
app_main()
  ├── initArduino()
  ├── setup()
  │   ├── SPI.begin()              → Hardware SPI bus
  │   ├── tft.initR()              → ST7735 display
  │   ├── ui.begin()               → TftUI framework
  │   ├── usb_drive_init()         → Mount FAT + TinyUSB MSC/CDC
  │   ├── sd_mount()               → Verify VFS ready
  │   ├── sd_load_metadata()       → Scan /usb dirs → FirmwareMap
  │   ├── [wait loop if no FW]     → Show instructions, wait for USB copy
  │   ├── ui_state_load()          → Restore saved tab/item from NVS
  │   ├── usb_drive_unlock()       → PC can now access drive
  │   ├── app_actions_init()       → Wire UI callbacks
  │   └── splash screen            → 1.5s delay
  └── loop()
      └── runTabsUI()              → blocking tab navigation
          └── handleTabSelection() → dispatch to action_*()
```

## 4. Flash Workflow (STM32 SWD)

```
action_flash_firmware(fw_id)
  ├── lookup metadata from FirmwareMap
  ├── usb_drive_lock()              → ESP32 owns FAT
  ├── swd_probe_idcode()            → detect M3(F1) vs M4(F4)
  ├── [if RDP] → confirm dialog → rdp_disable_trigger()
  │   ├── blind write unlock keys (KEYR + OPTKEYR + OPTCR)
  │   ├── wait 15s mass erase
  │   └── verify: reconnect + read flash
  ├── flash_firmware(path, progress_cb)
  │   ├── detect_rdp() as warm-up
  │   ├── full_connect() with 5x retry + DP ABORT clear
  │   ├── open firmware file
  │   ├── for each 256B chunk:
  │   │   ├── programBlock(addr, data, 256)
  │   │   ├── vTaskDelay(1)
  │   │   ├── dap_read_block(addr, verify_buf, 256)
  │   │   ├── memcmp → retry if mismatch (max 3x)
  │   │   └── progress_cb(text, percent)
  │   └── disconnect + deinit
  └── usb_drive_unlock()
```

## 5. USB Drive State Machine

```
                    ┌──────────┐
          init()    │  MOUNTED │  ← FAT VFS active
          ─────────►│ (APP)    │  ESP32 reads/writes
                    └────┬─────┘
                         │ unlock()
                    ┌────▼─────┐
                    │ UNLOCKED │  ← PC sees USB drive
                    │ (HOST)   │  PC reads/writes
                    └────┬─────┘
                         │ lock()
                    ┌────▼─────┐
                    │  LOCKED  │  ← ESP32 owns FAT
                    │ (APP)    │  PC read-only
                    └──────────┘
```

## 6. SWD Target Detection

```
swd_probe_idcode()
  ├── DAP init + connect
  ├── read IDCODE (DP register — always accessible)
  ├── disconnect + deinit
  └── return IDCODE

IDCODE routing (in app_actions):
  0x1BA01477 → Cortex-M3 → prog_stm32_f1 engine
  0x2BA01477 → Cortex-M4 → prog_stm32_f4 engine
  other      → ESP_FAIL (unsupported)
```

## 7. Component Dependencies

```
main ──► TftUI ──► Adafruit_ST7735 ──► Adafruit_GFX ──► Adafruit_BusIO
main ──► Adafruit_DAP (modified: STM32F1 + ESP32 compat)
main ──► esp_tinyusb (MSC + CDC)
main ──► fatfs + wear_levelling (FAT partition)
main ──► nvs_flash (UI state persistence)
main ──► espressif/arduino-esp32 (Arduino framework)
main ──► espressif/esp-serial-flasher (managed component, UART flash)
main ──► mbedtls (AES-128-CBC, part of ESP-IDF)
```

## 8. Key Design Decisions

1. **USB MSC over SD card** — eliminates SD card hardware, leverages ESP32-S3 native USB
2. **Bit-bang SWD over dedicated debugger** — no extra IC needed, ESP32 GPIO sufficient
3. **Per-chunk verify** — compensates for ~1/1000 SWD bit-bang error rate
4. **Callback-based UI decoupling** — flasher engines never import UI headers
5. **FAT lock/unlock** — prevents race condition between USB host writes and ESP32 file reads
6. **IDCODE-based routing** — single binary supports multiple STM32 families
7. **No OOP yet** — procedural C-style with static DAP instances per engine (OOP wrap planned)

## 9. Constraints & Limitations

- **Single-threaded flash** — SWD bit-bang requires `portENTER_CRITICAL`, no parallel operations
- **~12.9MB firmware store** — limited by FAT partition size
- **SWD speed** — bit-bang ~144us/word, ~3s for 15KB firmware (vs <0.5s with flash loader stub)
- **3-button UI** — no touch, no encoder, limited navigation
- **No OTA** — flasher firmware itself updated via USB serial only
