# Code Standards - ESP32 Multi-Flasher

## Codebase Structure

### Module Organization

**Pattern:** Function-based modular architecture
```
main/
├── [module_name]/
│   ├── [module_name].h     # Public API
│   └── [module_name].cpp   # Implementation
└── [module_name].h          # Shared types/constants
```

**Rationale:**
- Clear separation of concerns
- Self-contained modules with minimal coupling
- Easy to test and modify independently

### Include Order

**Standard Pattern:**
```cpp
// 1. System headers (ESP-IDF)
#include "esp_log.h"
#include "driver/uart.h"

// 2. Arduino framework (if needed)
#include "Arduino.h"

// 3. Project types/common
#include "firmware_types.h"

// 4. Module headers
#include "sd_card/sd_card.h"
#include "flasher/flasher.h"
```

## Naming Conventions

### Functions

**Pattern:** `module_verb_noun()`

**Examples:**
```cpp
esp_err_t sd_mount(int cs_pin);
esp_err_t flasher_init(void);
void menu_init(Adafruit_SSD1306& disp, ...);
bool wifi_config_connect();
void oled_show_message(const char* line1, const char* line2);
```

**Rules:**
- Module prefix mandatory (sd_, flasher_, menu_, wifi_, ota_, metadata_, sync_)
- Lowercase with underscores
- Verb-first for actions (mount, init, load, get, set)
- Noun-first for queries (status, info) rare in this codebase
- Boolean functions use `is_` or return bool directly

### Variables

**Global Variables:**
```cpp
extern bool g_is_sd_mounted;                              // g_ prefix
extern FirmwareMap g_firmware_map;                        // g_ prefix
static const char *TAG = "MODULE_NAME";                   // Logger tag
```

**Local Variables:**
```cpp
int selectedIndex;          // camelCase
const char* fw_id_char;     // camelCase
uint8_t buf[BUF_LEN];       // lowercase for arrays
```

**Member Variables (in structs):**
```cpp
std::string device_type;    // snake_case
std::string path_bootloader;
std::string md5_partition;
```

### Constants

**Pattern:** `ALL_CAPS` with module prefix
```cpp
#define FLASH_UART_TX_PIN      GPIO_NUM_0
#define ESP_BOOTLOADER_ADDR    0x1000
#define BUFFER_SIZE            4096
#define BTN_UP                 21
#define SCREEN_WIDTH           128
#define CONFIG_FILE_URL        "/config/url.txt"
```

### Types

**Struct/Typedef Pattern:** `lowercase_t` suffix
```cpp
typedef struct {
    std::string device_type;
    std::string version;
    std::string path;
    std::string md5;
    // ...
} firmware_metadata_t;
```

**Type Aliases:**
```cpp
using FirmwareMap = std::map<std::string, firmware_metadata_t>;
```

### Header Guards

**Mix of styles observed:**

**Style 1: Traditional (preferred in older modules)**
```cpp
#ifndef __FLASHER_H__
#define __FLASHER_H__
// ...
#endif // __FLASHER_H__
```

**Style 2: Pragma (used in newer modules)**
```cpp
#pragma once
// ...
```

**Standard:** Use `#pragma once` for new files, maintain existing style in legacy files

## File Naming

**Pattern:** `lowercase` with underscores

**Examples:**
```
main.cpp
firmware_types.h
sd_card.cpp
flasher.h
metadata_parser.cpp
ota_downloader.h
```

**Directory Names:** Match module name exactly
```
flasher/
sd_card/
wifi_config/
```

## Error Handling

### Return Codes

**Standard:** ESP-IDF `esp_err_t` for hardware operations
```cpp
esp_err_t sd_mount(int cs_pin) {
    if (!SD.begin(cs_pin)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
```

**Boolean:** For high-level logic operations
```cpp
bool wifi_config_connect() {
    if (WiFiManager_connect()) {
        return true;
    }
    return false;
}
```

### Logging

**Pattern:** ESP-IDF logging macros
```cpp
static const char *TAG = "MODULE_NAME";

ESP_LOGI(TAG, "Info message: %s", variable);
ESP_LOGW(TAG, "Warning message");
ESP_LOGE(TAG, "Error: %s", error_msg);
ESP_LOGD(TAG, "Debug data: %d", value);
```

**Log Levels:**
- `ESP_LOGI` - Normal operation status
- `ESP_LOGW` - Warnings, degraded operation
- `ESP_LOGE` - Errors, operation failures
- `ESP_LOGD` - Debug details (disabled in production)

### Error Messages

**OLED Feedback:**
```cpp
oled_show_message("Error", "SD Card Lost!");
oled_show_message("Failed!", "Check Log");
oled_show_message("SUCCESS!", "Chip Erased.");
```

**Pattern:**
- Line 1: Status (Error, Failed, SUCCESS, Flashing, etc.)
- Line 2: Detail/Action

## Code Organization

### Main Application Structure

**Pattern:** Arduino-style with ESP-IDF wrapper
```cpp
void setup() {
    // Hardware init
    // SD mount
    // Menu init
}

void loop() {
    // Poll menu
    // Dispatch commands
}

extern "C" void app_main() {
    initArduino();
    setup();
    while (true) {
        loop();
    }
}
```

### Module Structure

**Header File (.h):**
```cpp
#pragma once

// Includes
#include "dependency.h"

// Constants
#define MODULE_CONSTANT 123

// Types
typedef struct { ... } type_t;

// Global variable declarations
extern bool g_module_flag;

// Function prototypes
esp_err_t module_init(void);
esp_err_t module_do_something(params);
```

**Implementation File (.cpp):**
```cpp
#include "module.h"

// File-scoped constants
static const char *TAG = "MODULE";

// Global variable definitions
bool g_module_flag = false;

// Static helper functions
static void helper_function() {
    // Internal use only
}

// Public API implementations
esp_err_t module_init(void) {
    // Implementation
}
```

## Documentation Standards

### Function Comments

**Pattern:** Doxygen-style brief descriptions
```cpp
/**
 * @brief Mount SD card and initialize SPI interface.
 * @param cs_pin GPIO pin number for chip select.
 * @return ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t sd_mount(int cs_pin);
```

**Minimal Style (also used):**
```cpp
// === FUNCTION DESCRIPTION ===
// Brief explanation of what function does
esp_err_t function_name(params);
```

### Section Comments

**Pattern:** ASCII separators for major sections
```cpp
// ============================================================
// 1. ESP-IDF & SYSTEM HEADERS
// ============================================================

// ============================================================
// 2. PROJECT MODULES
// ============================================================

// --- BỘ ĐIỀU PHỐI (DISPATCHER) ---
```

**Usage:** Clearly separate logical sections in files

### Inline Comments

**Style:** Mix of English and Vietnamese
```cpp
// Kiểm tra thẻ nhớ lần nữa
if (sd_mount(SD_CS_PIN) != ESP_OK) {
    oled_show_message("Error", "SD Card Lost!");
    return;
}

// Read UART from Target and print to Log
int rxBytes = uart_read_bytes(UART_NUM_1, buf, BUF_LEN - 1, 20);
```

**Standard:** Prefer English for core logic, Vietnamese acceptable for UI/business logic

## Best Practices Observed

### Memory Management

**Strategy:** System restart after operations
```cpp
void host_system_restart() {
    oled_show_message("Restarting...", "Please wait");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}
```

**Rationale:**
- Prevents memory leaks in long-running operations
- Clears all state between operations
- Simplifies resource cleanup

**Pattern:** Call after every major operation:
- After flashing firmware
- After sync process
- After monitor mode exit
- After chip erase

### Hardware Initialization

**Pattern:** Init order matters
```cpp
void setup() {
    // 1. Logging first
    esp_log_level_set("*", ESP_LOG_INFO);

    // 2. Display (for error feedback)
    Wire.begin(SDA_PIN, SCL_PIN);
    display.begin(...);

    // 3. Critical dependencies
    sd_mount(SD_CS_PIN);

    // 4. Application state
    sd_load_metadata();
    menu_init(...);
}
```

**Rule:** Init in dependency order, fail fast on critical errors

### Button Debouncing

**Pattern:** Simple polling with delay
```cpp
if (digitalRead(BTN_UP) == LOW) {
    vTaskDelay(pdMS_TO_TICKS(300));  // Debounce
    // Handle button press
}
```

**Long Press Detection:**
```cpp
unsigned long start = millis();
while(digitalRead(BTN_UP) == LOW) {
    if(millis() - start > 3000) {
        // Long press detected
        break;
    }
}
```

### SD Card File Handling

**8.3 Filename Limitation:**
```cpp
// GOOD: Short names
"/FW_C3/boot.bin"
"/config/url.txt"

// BAD: Long names break FAT
"/ESP32_Firmware_V1.0/bootloader.bin"
```

**Rule:** Max 8 chars + 3 char extension due to FAT filesystem

### JSON Parsing

**Pattern:** Use ArduinoJson library
```cpp
DynamicJsonDocument doc(capacity);
deserializeJson(doc, json_string);
JsonObject obj = doc.as<JsonObject>();
for (JsonPair kv : obj) {
    // Process key-value pairs
}
```

**Memory:** Use DynamicJsonDocument with calculated capacity

### WiFi Operations

**Pattern:** Enable only when needed, disable after
```cpp
// Connect
wifi_config_connect();

// Use WiFi
sync_engine_run();

// Cleanup
wifi_config_stop();  // Saves power
```

### UART Configuration

**Flash Mode:**
```cpp
uart_set_baudrate(UART_NUM_1, FLASH_BAUD);  // High speed
```

**Monitor Mode:**
```cpp
uart_set_baudrate(UART_NUM_1, 115200);      // Standard
uart_flush_input(UART_NUM_1);               // Clear buffer
```

## Coding Anti-Patterns to Avoid

**1. Magic Numbers:**
```cpp
// BAD
vTaskDelay(500);

// GOOD
vTaskDelay(pdMS_TO_TICKS(500));
```

**2. Hardcoded Strings:**
```cpp
// BAD
if (fw_id == "Monitor") { ... }

// ACCEPTABLE (menu commands are design constants)
if (fw_id == "Monitor") { ... }  // OK: Special command ID
```

**3. Missing Error Checks:**
```cpp
// BAD
sd_mount(SD_CS_PIN);
sd_load_metadata();  // Will crash if mount failed

// GOOD
if (sd_mount(SD_CS_PIN) != ESP_OK) {
    oled_show_message("Error", "No SD Card");
    for(;;);  // Halt on critical error
}
```

**4. Memory Leaks:**
```cpp
// PROBLEM: Not an issue due to restart strategy
// System restarts after operations, clearing all allocations
```

## Build Compiler Flags

**Warning Suppression:**
```cmake
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
```

**Rationale:** Arduino/ESP-IDF callbacks have unused params by design

## Configuration Management

### Compile-Time Config

**sdkconfig:** ESP-IDF menuconfig options
- WiFi/BT enable
- Partition layout
- FreeRTOS settings
- Component features

### Runtime Config

**SD Card Files:**
- `/index.txt` - Firmware catalog (JSON)
- `/config/url.txt` - Remote server URL
- `/config/aes_key.txt` - Encryption key
- `/config/aes_iv.txt` - Encryption IV

**NVS Storage:**
- WiFi credentials (managed by WiFiManager)
- Last connected AP

## Language Mix

**Observed:** Vietnamese and English mixed in codebase
**Recommendation:**
- Core logic comments: English
- UI strings: Vietnamese (matches user base)
- Variable names: English (international standard)
- Documentation: Vietnamese README, English technical docs

## Code Review Checklist

**Before Commit:**
- [ ] Module prefix on all functions
- [ ] Error handling with esp_err_t or bool
- [ ] Logging with appropriate ESP_LOG level
- [ ] OLED feedback for user-visible operations
- [ ] Memory cleanup or restart after operation
- [ ] 8.3 filename compliance for SD paths
- [ ] Button debounce delays present
- [ ] NULL checks before pointer dereference
- [ ] Build with no errors/warnings
- [ ] Test on actual hardware (SD, OLED, buttons)

## Recommended Improvements

### Short-term (v1.1)
1. **Header Guards:** Standardize on `#pragma once` for all new files
2. **Comments:** Translate critical Vietnamese comments to English for international contributors
3. **Logging:** Add module-specific log levels (currently all INFO)
4. **Error Handling:** Implement retry logic for flash operations (currently fail-fast)

### Mid-term (v1.2)
5. **Unit Tests:** Integrate Google Test framework for module testing
6. **Static Analysis:** Add Clang-Tidy to CI/CD pipeline
7. **Code Formatting:** Adopt clang-format with project .clang-format config
8. **Doxygen:** Generate API docs automatically from comments

### Long-term (v2.0)
9. **Pure C++ Style:** Migrate from C-style to modern C++17 patterns
10. **RAII:** Use smart pointers instead of manual memory management
11. **Const Correctness:** Add const to all read-only parameters
12. **Namespace:** Wrap modules in namespace esp_flasher

## Style Guide Summary

**Quick Reference:**
```cpp
// File: module_name/module_name.h
#pragma once
#include "esp_log.h"
#include "dependency.h"

#define MODULE_CONSTANT 123

typedef struct {
    std::string field_name;
} struct_name_t;

extern bool g_global_variable;

esp_err_t module_init(void);
esp_err_t module_action_name(const type& param);

// File: module_name/module_name.cpp
#include "module_name.h"

static const char *TAG = "MODULE_NAME";

bool g_global_variable = false;

static void helper_function() {
    // Implementation
}

esp_err_t module_init(void) {
    ESP_LOGI(TAG, "Initializing module");
    return ESP_OK;
}
```

## Unresolved Standards Questions

1. Should all Vietnamese comments be translated to English? (Current: keep user-facing text in VN)
2. Maximum line length enforcement? (Current: no limit, suggest 100-120 chars)
3. Doxygen documentation completeness requirement? (Current: optional, suggest mandatory for public API)
4. Unit testing coverage target? (Current: 0%, suggest >60% for v1.2)
5. Static analysis baseline (Clang-Tidy warnings to fix vs suppress)?
6. Code formatting enforcement (pre-commit hook vs manual)?
7. C++ version target (Current: C++11, upgrade to C++17)?
8. Const correctness enforcement level (strict vs pragmatic)?
9. Include order enforcement tool (include-what-you-use)?
10. Naming convention for template types and concepts (if upgraded to C++20)?

## Version

**Document Version:** 1.1
**Last Updated:** 2025-11-27
**Applies To:** ESP32 Multi-Flasher v1.0.0+
