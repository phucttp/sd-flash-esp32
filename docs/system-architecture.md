# System Architecture - ESP32 Multi-Flasher

## Overview

ESP32 Multi-Flasher is modular embedded system combining offline SD card-based firmware flashing with online WiFi synchronization. Architecture follows separation of concerns with clear boundaries between hardware abstraction, business logic, and user interface layers.

---

## System Block Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        ESP32-C3 HOST                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌──────────┐ │
│  │   OLED     │  │  SD Card   │  │  Buttons   │  │   WiFi   │ │
│  │ SSD1306    │  │   (SPI)    │  │  (GPIO)    │  │ (Radio)  │ │
│  │   (I2C)    │  │            │  │            │  │          │ │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘  └────┬─────┘ │
│        │               │               │              │        │
│  ┌─────┴───────────────┴───────────────┴──────────────┴─────┐  │
│  │              Hardware Abstraction Layer                   │  │
│  │  (Wire, SD, GPIO, WiFi, UART)                             │  │
│  └─────┬───────────────┬───────────────┬──────────────┬─────┘  │
│        │               │               │              │        │
│  ┌─────┴──────┐  ┌─────┴──────┐  ┌─────┴──────┐  ┌───┴─────┐  │
│  │  OLED/Menu │  │  SD Card   │  │  Flasher   │  │  WiFi   │  │
│  │   Module   │  │  Module    │  │  Module    │  │ Config  │  │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘  └───┬─────┘  │
│        │               │               │              │        │
│  ┌─────┴───────────────┴───────────────┴──────────────┴─────┐  │
│  │            Orchestration Layer (main.cpp)                 │  │
│  │  - Command Dispatcher                                     │  │
│  │  - System Lifecycle Management                            │  │
│  └─────┬───────────────┬───────────────┬──────────────┬─────┘  │
│        │               │               │              │        │
│  ┌─────┴──────┐  ┌─────┴──────┐  ┌─────┴──────┐  ┌───┴─────┐  │
│  │ Flash FW   │  │ Sync FW    │  │  Monitor   │  │  Erase  │  │
│  │  Command   │  │  Command   │  │  Command   │  │ Command │  │
│  └────────────┘  └────────────┘  └────────────┘  └─────────┘  │
│                                                                  │
└─────────────────────────┬────────────────────────────────────────┘
                          │ UART (TX/RX/EN/BOOT)
                          ▼
                   ┌──────────────┐
                   │  ESP32 Target │
                   │  (Being      │
                   │   Flashed)   │
                   └──────────────┘
```

---

## Module Architecture

### Layer 1: Hardware Abstraction

**Purpose:** Isolate hardware-specific code, provide consistent API

#### 1.1 OLED/Menu Module (`oled/menu.cpp`)

**Responsibilities:**
- Initialize SSD1306 display via I2C
- Manage menu state (scroll position, selection)
- Poll button inputs with debouncing
- Render menu items and status messages

**Hardware Interface:**
```
I2C:  SDA=GPIO8, SCL=GPIO9, Addr=0x3C
GPIO: BTN_UP=GPIO21, BTN_DOWN=GPIO20, BTN_OK=GPIO10
```

**API:**
```cpp
void menu_init(Adafruit_SSD1306& disp, const char* displayItems[],
               const char* idItems[], int len);
int menu_update();                    // Returns selected index or -1
const char* menu_get_id(int index);
void oled_show_message(const char* line1, const char* line2);
```

**State Management:**
- Current scroll position (scrollIndex)
- Total menu items (menuLength)
- Display vs ID item arrays (parallel)

#### 1.2 SD Card Module (`sd_card/sd_card.cpp`)

**Responsibilities:**
- Mount/unmount SD card via SPI
- Load and parse JSON index.txt
- Provide firmware metadata lookup
- Generate menu arrays from firmware map

**Hardware Interface:**
```
SPI: CS=GPIO7, MOSI/MISO/SCK via SPI peripheral
```

**API:**
```cpp
esp_err_t sd_mount(int cs_pin);
esp_err_t sd_unmount();
esp_err_t sd_load_metadata();
esp_err_t sd_get_firmware_path(const string& fw_id, firmware_metadata_t& out);
const char** sd_get_menu_display_items(int& out_count);
const char** sd_get_menu_id_items();
```

**Global State:**
```cpp
bool g_is_sd_mounted;
FirmwareMap g_firmware_map;  // map<string, firmware_metadata_t>
```

#### 1.3 Flasher Module (`flasher/flasher.cpp`)

**Responsibilities:**
- Initialize UART for serial flasher protocol
- Control Target boot mode (EN, BOOT pins)
- Write bootloader/partition/app to Target flash
- Verify flash via MD5 checksums
- Erase Target chip

**Hardware Interface:**
```
UART:  TX=GPIO0→Target_RX, RX=GPIO1←Target_TX
GPIO:  RESET=GPIO2→Target_EN, BOOT=GPIO3→Target_GPIO0
```

**API:**
```cpp
esp_err_t flasher_init();
esp_err_t flasher_begin_session(const string& fw_id);
esp_err_t flasher_write_segment(const string& path, uint32_t offset, const string& md5);
esp_err_t flasher_chip_erase();
void host_system_restart();
```

**Flash Memory Map (Target):**
```
0x1000   → Bootloader
0x8000   → Partition Table
0x10000  → Application
```

#### 1.4 WiFi Config Module (`wifi_config/wifi_config.cpp`)

**Responsibilities:**
- Connect to WiFi (auto-connect or captive portal)
- Load configuration from SD (/config/*.txt)
- Manage WiFiManager portal
- Disconnect WiFi to save power

**Hardware Interface:**
```
WiFi Radio: 2.4GHz only
```

**API:**
```cpp
bool wifi_config_connect();
void wifi_config_force_portal();
void wifi_config_get_params(char* url, char* key, char* iv);
void wifi_config_stop();
```

**Configuration Files:**
```
/config/url.txt      → Remote server URL
/config/aes_key.txt  → AES-128 key (16 bytes)
/config/aes_iv.txt   → AES-128 IV (16 bytes)
```

---

### Layer 2: Business Logic

#### 2.1 Metadata Parser (`metadata_parser/metadata_parser.cpp`)

**Purpose:** Parse JSON firmware catalog into FirmwareMap

**API:**
```cpp
bool metadata_parse_json(const String& json_content, FirmwareMap& out_map);
```

**Input Format:**
```json
{
  "FW_ID": {
    "device_type": "ESP32-C3",
    "version": "1.0.0",
    "path": "/path/app.bin",
    "md5": "...",
    "path_bootloader": "/path/boot.bin",
    "md5_bootloader": "...",
    "path_partition": "/path/part.bin",
    "md5_partition": "..."
  }
}
```

**Output:** Populates FirmwareMap with parsed metadata

#### 2.2 OTA Downloader (`ota_downloader/ota_downloader.cpp`)

**Purpose:** HTTP download with AES-128-CBC decryption

**API:**
```cpp
string ota_download_index(const string& url, const string& save_path);
bool ota_download_file_encrypted(const string& url, const string& save_path,
                                  const string& key, const string& iv);
```

**Features:**
- Streaming HTTP(S) download
- Real-time AES decryption (mbedtls)
- Direct write to SD card
- Progress feedback via OLED

**Encryption:**
```
Algorithm: AES-128-CBC
Key Size:  16 bytes
IV Size:   16 bytes
Padding:   PKCS#7
```

#### 2.3 Sync Engine (`sync_engine/sync_engine.cpp`)

**Purpose:** Orchestrate firmware synchronization workflow

**API:**
```cpp
void sync_engine_run(bool force_clean);
```

**Workflow:**
```
1. Download remote index.txt
2. Parse remote JSON
3. Load local index from SD
4. Compare firmware entries:
   - Different MD5 → Download .enc file
   - Missing locally → Download
   - Missing remotely → Delete (if force_clean)
5. Decrypt and save files
6. Update local index.txt
7. Report completion
```

---

### Layer 3: Orchestration

#### 3.1 Main Application (`main/main.cpp`)

**Architecture:** Arduino-style with ESP-IDF wrapper

**Entry Points:**
```cpp
void setup()           // Init hardware, load metadata, init menu
void loop()            // Poll menu, dispatch commands
extern "C" void app_main()  // ESP-IDF entry point
```

**Command Dispatcher:**
```cpp
Selected ID → Action Mapping:
- "Monitor"   → run_monitor_mode()
- "SyncFW"    → run_sync_process()
- "Erase"     → run_chip_erase()
- Other       → run_flash_fw(fw_id)
```

**System Lifecycle:**
```
Boot → Init → Menu Loop → Command → Restart
                  ▲                     │
                  └─────────────────────┘
```

**Memory Management Strategy:**
- Restart after every operation
- Prevents memory leaks and fragmentation
- Forces clean state for next operation

---

## Data Flow Diagrams

### Offline Flash Flow

```
┌──────────┐
│  User    │
│ (Buttons)│
└────┬─────┘
     │ Press UP/DOWN/OK
     ▼
┌────────────┐
│ Menu Module│ ←──── Display Items ──── SD Card Module
└────┬───────┘                              ↑
     │ Selected Index                       │
     ▼                                      │ JSON Parse
┌────────────┐                         ┌────┴──────┐
│ main.cpp   │ ──── Get FW ID ──────→ │ Metadata  │
│ (dispatch) │                         │  Parser   │
└────┬───────┘                         └───────────┘
     │ fw_id
     ▼
┌────────────────┐
│ Flasher Module │
└────┬───────────┘
     │ Read .bin from SD
     ▼
┌────────────┐     UART Protocol      ┌──────────┐
│ SD Card    │ ────────────────────→  │ Target   │
│ (file I/O) │                         │ ESP32    │
└────────────┘                         └──────────┘
     ▲
     │ MD5 Verify
     │
┌────────────┐
│ OLED       │ ←──── Progress % ──── Flasher
│ (feedback) │
└────────────┘
```

### Online Sync Flow

```
┌──────────┐
│  User    │
│(Select   │
│ "Sync")  │
└────┬─────┘
     │
     ▼
┌─────────────┐
│ WiFi Config │ ──→ Connect to AP
└─────┬───────┘
      │ WiFi Connected
      ▼
┌─────────────┐        HTTP GET         ┌──────────────┐
│ OTA         │ ────────────────────→   │ Remote       │
│ Downloader  │                          │ Server       │
└─────┬───────┘ ←────── index.txt ───── │ (GitHub)     │
      │                                  └──────────────┘
      │ Download
      ▼
┌─────────────┐
│ Metadata    │ ──→ Parse Remote JSON
│ Parser      │
└─────┬───────┘
      │ FirmwareMap (remote)
      ▼
┌─────────────┐
│ Sync Engine │ ──→ Compare with Local
└─────┬───────┘
      │ Diff List
      ▼
┌─────────────┐        HTTP GET         ┌──────────────┐
│ OTA         │ ────────────────────→   │ Remote       │
│ Downloader  │                          │ Server       │
└─────┬───────┘ ←────── .enc files ──── └──────────────┘
      │
      │ Stream Decrypt (AES-128-CBC)
      ▼
┌─────────────┐
│ SD Card     │ ──→ Save decrypted .bin files
│ Module      │
└─────────────┘
      │
      │ Update index.txt
      ▼
┌─────────────┐
│ System      │ ──→ Restart Host
│ Restart     │
└─────────────┘
```

### Monitor Mode Flow

```
┌──────────┐
│  User    │
│ (Select  │
│"Monitor")│
└────┬─────┘
     │
     ▼
┌─────────────┐
│ main.cpp    │ ──→ run_monitor_mode()
└─────┬───────┘
      │
      ▼
┌─────────────┐
│ Flasher     │ ──→ Init UART @ 115200 baud
│ Module      │
└─────┬───────┘
      │
      │ UART RX Loop
      ▼
┌─────────────┐     Serial Data      ┌──────────┐
│ UART Buffer │ ←──────────────────  │ Target   │
│ (128 bytes) │                       │ ESP32    │
└─────┬───────┘                       └──────────┘
      │
      │ ESP_LOGI("TARGET", ...)
      ▼
┌─────────────┐
│ ESP-IDF     │ ──→ Output to Host UART
│ Log System  │
└─────────────┘
      ▲
      │ Press OK to Exit
┌─────┴───────┐
│ Button Poll │
└─────────────┘
```

---

## State Machine Diagram

### Main Application State Machine

```
                    ┌──────────────┐
                    │   BOOT       │
                    │              │
                    └──────┬───────┘
                           │ setup()
                           ▼
                    ┌──────────────┐
              ┌────→│   MENU_IDLE  │←────┐
              │     │              │     │
              │     └──────┬───────┘     │
              │            │ User Select │
              │            ▼             │
              │     ┌──────────────┐     │
              │     │ FLASH_FW     │     │
              │     └──────┬───────┘     │
              │            │             │
              │            ▼             │
              │     ┌──────────────┐     │
              │     │ RESTART      │─────┘
              │     └──────────────┘
              │
              │     ┌──────────────┐
              ├────→│ SYNC_FW      │
              │     └──────┬───────┘
              │            │
              │            ▼
              │     ┌──────────────┐
              │     │ RESTART      │─────┘
              │     └──────────────┘
              │
              │     ┌──────────────┐
              ├────→│ MONITOR      │
              │     └──────┬───────┘
              │            │ OK Press
              │            ▼
              │     ┌──────────────┐
              │     │ RESTART      │─────┘
              │     └──────────────┘
              │
              │     ┌──────────────┐
              └────→│ ERASE_CHIP   │
                    └──────┬───────┘
                           │
                           ▼
                    ┌──────────────┐
                    │ RESTART      │─────┘
                    └──────────────┘
```

---

## Memory Architecture

### RAM Usage Strategy

**Critical Design Decision:** System restarts after each operation

**Memory Allocation:**
```
Static:
- OLED display object      (~200 bytes)
- FirmwareMap              (dynamic, ~2KB typical)
- Menu arrays              (~1KB)
- UART buffer              (128 bytes)

Dynamic (operation-specific):
- JSON parsing buffer      (~4KB)
- Flash read buffer        (4KB)
- HTTP client context      (~8KB during sync)
- AES context              (~200 bytes)
```

**Lifecycle:**
```
Boot → Allocate Static → Load Metadata → Menu Loop
                                              │
        ┌─────────────────────────────────────┘
        │
        ▼
   Operation (allocate dynamic buffers)
        │
        ▼
   Restart (free ALL memory)
        │
        └──→ Back to Boot
```

**Rationale:**
- Prevents memory fragmentation from WiFi/HTTP operations
- Eliminates risk of memory leaks in long-running operations
- Simplifies error recovery (always clean state)
- No need for complex cleanup logic

---

## Hardware Abstraction Layers

### I2C Bus (OLED)
```
Wire.begin(SDA_PIN, SCL_PIN) → Adafruit_SSD1306 → menu.cpp
```

### SPI Bus (SD Card)
```
SD.begin(CS_PIN) → SD library → sd_card.cpp
```

### GPIO (Buttons)
```
pinMode(BTN_x, INPUT_PULLUP) → digitalRead() → menu.cpp
```

### UART (Flasher & Monitor)
```
uart_driver_install() → esp-serial-flasher → flasher.cpp
                     → uart_read_bytes()   → monitor mode
```

### WiFi (Radio)
```
WiFiManager → WiFi.h → wifi_config.cpp
           → WiFiClient → esp_http_client → ota_downloader.cpp
```

### Crypto (AES)
```
mbedtls_aes_setkey() → mbedtls_aes_crypt_cbc() → ota_downloader.cpp
```

---

## Boot Sequence

```
┌─────────────────────────────────────────────┐
│ 1. ESP-IDF Bootloader                       │
│    - Load partition table                   │
│    - Jump to app                            │
└─────────────┬───────────────────────────────┘
              ▼
┌─────────────────────────────────────────────┐
│ 2. app_main() [ESP-IDF entry point]         │
│    - initArduino()                          │
│    - Call setup()                           │
└─────────────┬───────────────────────────────┘
              ▼
┌─────────────────────────────────────────────┐
│ 3. setup()                                  │
│    a. Set log level                         │
│    b. Init I2C (Wire.begin)                 │
│    c. Init OLED (display.begin)             │
│    d. Show "Booting..." message             │
└─────────────┬───────────────────────────────┘
              ▼
┌─────────────────────────────────────────────┐
│ 4. SD Card Mount                            │
│    - sd_mount(SD_CS_PIN)                    │
│    - Check mount success                    │
│    - Halt if failed (critical)              │
└─────────────┬───────────────────────────────┘
              ▼
┌─────────────────────────────────────────────┐
│ 5. Load Metadata                            │
│    - sd_load_metadata()                     │
│    - Open /index.txt                        │
│    - Parse JSON                             │
│    - Populate g_firmware_map                │
└─────────────┬───────────────────────────────┘
              ▼
┌─────────────────────────────────────────────┐
│ 6. Build Menu Arrays                        │
│    - sd_get_menu_display_items()            │
│    - sd_get_menu_id_items()                 │
│    - Check menuLen > 0                      │
└─────────────┬───────────────────────────────┘
              ▼
┌─────────────────────────────────────────────┐
│ 7. Init Menu UI                             │
│    - menu_init(display, items, ids, len)    │
│    - Set up button GPIOs                    │
│    - Render initial menu                    │
└─────────────┬───────────────────────────────┘
              ▼
┌─────────────────────────────────────────────┐
│ 8. Enter loop()                             │
│    - Poll menu                              │
│    - Dispatch commands                      │
│    - (Never returns, runs forever)          │
└─────────────────────────────────────────────┘
```

**Boot Time:** ~2-3 seconds from power-on to menu ready

---

## Inter-Process Communication

**Pattern:** Synchronous function calls (no IPC/queues)

**Module Dependencies:**
```
main.cpp
├── menu → OLED display, button GPIO
├── sd_card → SD SPI, JSON parsing
├── flasher → UART, GPIO (EN/BOOT)
├── wifi_config → WiFi radio, SD files
├── ota_downloader → WiFi, SD, mbedtls
├── metadata_parser → ArduinoJson
└── sync_engine → All above modules
```

**Global Shared State:**
```cpp
// sd_card.cpp
extern bool g_is_sd_mounted;
extern FirmwareMap g_firmware_map;

// main.cpp
bool force_delete;  // Sync mode flag
Adafruit_SSD1306 display;  // Shared OLED instance
```

**No Task Concurrency:**
- Single-threaded Arduino loop() model
- No FreeRTOS tasks created by application
- All operations blocking/sequential
- Simplifies synchronization (no mutexes needed)

---

## Error Handling Architecture

### Critical Errors (Halt System)
```cpp
if (sd_mount(SD_CS_PIN) != ESP_OK) {
    oled_show_message("Error", "No SD Card");
    for(;;);  // Infinite loop, system halted
}
```

**Handled:**
- SD card mount failure
- OLED init failure
- Empty menu (no firmware)

### Recoverable Errors (Show Message + Restart)
```cpp
if (flasher_begin_session(fw_id) != ESP_OK) {
    oled_show_message("Failed!", "Check Log");
    vTaskDelay(2000);
    host_system_restart();
}
```

**Handled:**
- Flash operation failure
- WiFi connection timeout
- Download failure
- JSON parse error

### User Feedback Channels
1. **OLED:** Primary feedback (status, errors, progress)
2. **ESP-IDF Logs:** Detailed diagnostic info via UART (115200 baud)
3. **LED (future):** Status indicator (not implemented)

---

## Configuration Management

### Compile-Time Configuration
**File:** `sdkconfig`
- Target chip (ESP32-C3)
- WiFi/BT enable
- Partition table layout
- FreeRTOS settings
- Component features

### Runtime Configuration (SD Card)
```
/index.txt           → Firmware catalog
/config/url.txt      → Remote server URL
/config/aes_key.txt  → AES encryption key
/config/aes_iv.txt   → AES initialization vector
```

### Persistent Storage (NVS)
- WiFi credentials (managed by WiFiManager)
- Last connected SSID
- WiFiManager portal settings

---

## Security Architecture

### Encrypted Firmware Downloads
```
Remote Server (.enc files)
         │
         │ HTTPS (optional)
         ▼
   OTA Downloader
         │
         │ AES-128-CBC Decrypt (streaming)
         ▼
   SD Card (.bin files)
         │
         │ Flash to Target
         ▼
   Target ESP32
```

**Encryption Details:**
- Algorithm: AES-128-CBC
- Key management: Stored on SD card (plaintext)
- IV: Unique per file (recommended)
- Padding: PKCS#7

**Limitations:**
- AES keys stored in plaintext on SD (physical access risk)
- No certificate validation for HTTPS (configurable)
- No secure boot on Host (ESP32-C3 supports, not enabled)

### MD5 Verification
```
Flash .bin to Target
         │
         ▼
   Read back flash
         │
         ▼
   Calculate MD5
         │
         ▼
   Compare with metadata
         │
         ├──→ Match: Success
         └──→ Mismatch: Error
```

---

## Performance Characteristics

### Flash Speed
- **UART Baud:** High-speed (configurable, default from esp-serial-flasher)
- **Buffer Size:** 4KB
- **Typical Time:** ~30s for 1MB firmware (depends on baud rate)

### Sync Speed
- **Download:** Limited by WiFi bandwidth (~500KB/s typical)
- **Decryption:** Real-time streaming (AES-128 fast)
- **Write to SD:** ~200KB/s (SPI mode)

### Menu Responsiveness
- **Button Debounce:** 300ms delay
- **Long Press Detection:** 3 seconds
- **Menu Redraw:** <50ms

### Boot Time
- **Cold Boot:** ~2-3 seconds to menu
- **Post-Flash Restart:** ~2 seconds

---

## Future Architecture Considerations

### Potential Enhancements
1. **Task-based Architecture:** FreeRTOS tasks for concurrent operations
2. **Watchdog Timer:** Auto-recovery from hangs
3. **Secure Boot:** ESP32-C3 secure boot support
4. **Flash Encryption:** Protect Host firmware
5. **HTTPS Certificate Validation:** Verify remote server identity
6. **Logging to SD:** Persistent error logs
7. **Multi-Target Support:** Flash multiple targets in sequence

### Scalability Limits
- **Firmware Count:** Menu limited by display size (32px height = ~2 items visible)
- **Metadata Size:** JSON parsing limited by available RAM (~32KB free typical)
- **SD Card Size:** No limit (FAT32 supports up to 32GB)

---

## Compliance & Standards

### ESP-IDF Standards
- Component registration via `idf_component_register()`
- Logging via `ESP_LOG*()` macros
- Error codes via `esp_err_t` enum

### Arduino Compatibility
- `setup()` / `loop()` pattern
- Arduino library API (Wire, SD, WiFi)
- Wrapped in `extern "C" void app_main()`

### File System Standards
- FAT32 filesystem (8.3 filename format)
- JSON for metadata (RFC 8259)
- HTTP/1.1 for downloads (RFC 2616)

---

**Document Version:** 1.0
**Last Updated:** 2025-11-27
