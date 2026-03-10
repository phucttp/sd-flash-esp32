# Code Standards — CHIVI FlashPorter TFT

> Last updated: 260307

## 1. Language & Framework

- **C++ (C++11)** on ESP-IDF v5.1.6 + Arduino component
- ESP-IDF APIs for hardware (GPIO, SPI, NVS, FreeRTOS)
- Arduino APIs for convenience (SPI.begin, digitalRead, Serial)
- Entry: `extern "C" void app_main()` → `initArduino()` → `setup()` / `loop()`

## 2. Project Structure Convention

```
main/
├── main.cpp                # Entry point only — no business logic
├── pin_config.h            # ALL GPIO definitions (centralized)
├── firmware_types.h        # Shared type definitions
└── modules/
    ├── prog/               # Flasher engines (target programming)
    │   ├── prog_common.*       # Shared across all engines
    │   ├── esp32/              # ESP32 UART engine
    │   └── stm32/              # STM32 SWD engines (F1, F4)
    ├── storage/            # Data persistence (USB drive, firmware store)
    ├── ui/                 # User-facing logic (actions, state)
    └── utils/              # Cross-cutting utilities
```

**Rules:**
- Each module = directory with `.cpp` + `.h` pair
- Module headers use `#pragma once`
- Cross-module includes use relative paths (`../../storage/sd_card/sd_card.h`)
- No circular dependencies between layers

## 3. Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Files | `snake_case` | `prog_stm32_f4.cpp` |
| Functions | `snake_case` | `flasher_swd_stm32f4_init()` |
| Types/Structs | `snake_case_t` | `firmware_metadata_t` |
| Typedefs (map) | `PascalCase` | `FirmwareMap` |
| Constants | `UPPER_SNAKE` | `SWD_PROGRAM_CHUNK` |
| Macros | `UPPER_SNAKE` | `FLASH_PIN_0` |
| Static globals | `s_prefix` | `s_saved_tab` |
| ESP log tags | `static const char *TAG` | `"SWD_F4"` |

## 4. Include Order

```cpp
// 1. Own header (for .cpp files)
#include "prog_stm32_f4.h"

// 2. ESP-IDF / system headers
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

// 3. Arduino headers
#include "Arduino.h"

// 4. Library headers
#include "Adafruit_DAP.h"

// 5. Project modules (relative paths)
#include "../../storage/usb_drive/usb_drive.h"
```

## 5. Error Handling

- Functions return `esp_err_t` (`ESP_OK` / `ESP_FAIL`)
- Use `ESP_LOGE`, `ESP_LOGW`, `ESP_LOGI` for logging (never `Serial.println`)
- Critical errors: show message on TFT via callback, then return error code
- SWD operations: retry with `dap_disconnect()` + delay between attempts

## 6. FreeRTOS Usage

- `vTaskDelay(pdMS_TO_TICKS(ms))` for delays (never `delay()` blocking)
- `portENTER_CRITICAL(&mux)` / `portEXIT_CRITICAL(&mux)` for SWD bit-bang atomicity
- No `noInterrupts()` — crashes ESP32 FreeRTOS scheduler

## 7. Format Strings

- `-Werror=format` enabled — format mismatches are fatal
- `uint32_t`: use `PRIx32`, `PRIu32` (from `<inttypes.h>`)
- Never `%d` for unsigned types

## 8. Memory Management

- Prefer stack allocation for small buffers
- `malloc()` + check NULL for large dynamic allocations (firmware buffers)
- Dual RAM strategy for flash: try full-file malloc, fallback to 32KB streaming segments
- No C++ `new`/`delete` in flasher engines — C-style allocation

## 9. SWD-Specific Standards

- Always clear DP sticky errors (`dap_write_reg(0x00, 0x1E)`) before AHB-AP transactions
- Retry loop pattern: 5 attempts with `dap_disconnect()` + 200ms between retries
- Per-chunk verify mandatory (256B write → delay → read-back → memcmp → retry)
- `detect_rdp()` as SWD warm-up before actual flash connection

## 10. UI Callback Pattern

```cpp
// UI module provides callback types
typedef void (*action_progress_cb_t)(const char* text, int percent);
typedef bool (*action_confirm_cb_t)(const char* title, const char* message);

// App actions receive callbacks via config struct at init
app_actions_config_t cfg = { .on_progress = ui_progress_cb, ... };
app_actions_init(&cfg);

// Flasher engines receive callbacks as function parameters
flasher_swd_stm32f4_flash_firmware(path, progress_cb);
```

## 11. Build

```bash
. $IDF_PATH/export.sh        # or use ESP-IDF terminal
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

## 12. Git Conventions

- Commit prefix: `feat:`, `fix:`, `chore:`, `docs:`, `refactor:`
- Branch: `main` (single branch workflow)
