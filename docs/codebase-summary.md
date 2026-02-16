# Codebase Summary - ESP32 Multi-Flasher

## Directory Structure

```
ESP_MUL/Muti/
├── build/                      # ESP-IDF build output (generated)
├── components/                 # External components
│   ├── Adafruit_BusIO/        # I2C/SPI abstraction library
│   ├── Adafruit_DAP/          # SWD/JTAG debug probe (modified for ESP32-C3)
│   ├── Adafruit_GFX/          # Graphics core library
│   ├── Adafruit_SSD1306/      # OLED display driver
│   └── WiFiManager/           # WiFi configuration portal
├── main/                       # Application source code
│   ├── app_actions/           # Action dispatcher (flash, erase, etc.)
│   │   └── app_actions.cpp
│   ├── flasher/               # Flash engines (UART + SWD)
│   │   ├── flasher.cpp        # ESP32 UART flasher
│   │   ├── flasher.h
│   │   ├── flasher_common.h   # Shared pin definitions
│   │   ├── flasher_swd.cpp    # STM32 SWD flasher (~742 lines)
│   │   └── flasher_swd.h
│   ├── metadata_parser/       # JSON parsing module
│   │   ├── metadata_parser.cpp
│   │   └── metadata_parser.h
│   ├── oled/                  # Display & menu UI
│   │   ├── menu.cpp
│   │   └── menu.h
│   ├── ota_downloader/        # HTTP download & AES decrypt
│   │   ├── ota_downloader.cpp
│   │   └── ota_downloader.h
│   ├── sd_card/               # SD card management
│   │   ├── sd_card.cpp
│   │   └── sd_card.h
│   ├── sync_engine/           # Firmware sync orchestrator
│   │   ├── sync_engine.cpp
│   │   └── sync_engine.h
│   ├── wifi_config/           # WiFi & config management
│   │   ├── wifi_config.cpp
│   │   └── wifi_config.h
│   ├── firmware_types.h       # Shared data structures
│   ├── main.cpp               # Application entry point
│   ├── CMakeLists.txt         # Build configuration
│   ├── idf_component.yml      # IDF component manifest
│   └── Kconfig.projbuild      # Project configuration options
├── docs/                       # Documentation (this directory)
├── toolAddFirmware/           # FlashPorter PC Tool (Python/Tkinter)
│   ├── FlashPorter.py         # Main GUI application (~990 lines)
│   ├── esp_encryptor.py       # AES-128-CBC encryption utility
│   ├── tool_setting.json      # Saved configuration (key, IV, paths)
│   ├── firmware_library/      # Local firmware storage
│   ├── _release_for_git/      # Encrypted output for GitHub
│   ├── dist/
│   │   └── FlashPorter.exe    # Compiled Windows executable
│   ├── build/                 # PyInstaller build artifacts
│   └── icons/logo.png         # Application icon
├── CMakeLists.txt             # Root build file
├── sdkconfig                  # ESP-IDF configuration
└── README.md                  # Project README (Vietnamese)
```

## Key Source Files

### Core Application (`main/main.cpp`)
**Purpose:** Application orchestrator and entry point
**Responsibilities:**
- Arduino-style setup() and loop() structure
- System initialization (OLED, SD, menu)
- Command dispatcher (flash, sync, monitor, erase)
- FreeRTOS integration via app_main()

**Key Functions:**
- `setup()` - Init hardware, mount SD, load metadata, init menu
- `loop()` - Menu polling, command dispatch
- `run_flash_fw()` - Execute firmware flash operation
- `run_sync_process()` - Execute WiFi sync operation
- `run_monitor_mode()` - UART log viewer mode
- `run_chip_erase()` - Target chip erase
- `app_main()` - ESP-IDF entry point wrapper

**Global Variables:**
- `display` - Adafruit_SSD1306 OLED object
- `force_delete` - Flag for force clean sync
- `buf[BUF_LEN]` - UART read buffer

### ESP32 UART Flasher (`main/flasher/flasher.cpp`)
**Purpose:** Low-level UART flashing via esp-serial-flasher
**API:**
```cpp
esp_err_t flasher_init();                           // Init UART & GPIO
esp_err_t flasher_begin_session(const string& fw_id); // Flash complete firmware
esp_err_t flasher_write_segment(path, offset, md5); // Flash single binary
esp_err_t flasher_chip_erase();                     // Erase target chip
void host_system_restart();                         // Restart host ESP32
```

**Hardware Configuration:**
- TX_PIN: GPIO 0 → Target RX
- RX_PIN: GPIO 1 → Target TX
- RESET_PIN: GPIO 2 → Target EN
- BOOT_PIN: GPIO 3 → Target GPIO0

**Flash Addresses:**
- Bootloader: 0x1000
- Partition Table: 0x8000
- Application: 0x10000
- Buffer Size: 4096 bytes

### STM32 SWD Flasher (`main/flasher/flasher_swd.cpp`)
**Purpose:** STM32F4 flash programming via SWD bit-bang (Adafruit_DAP)
**API:**
```cpp
esp_err_t flasher_swd_init();                       // Init SWD GPIO pins
esp_err_t flasher_swd_deinit();                     // Release SWD pins
esp_err_t flasher_swd_detect_rdp(int *rdp_level);   // Detect RDP level (0/1/2)
esp_err_t flasher_swd_rdp_disable_trigger();         // Blind-write RDP disable
esp_err_t flasher_swd_rdp_disable_verify();          // Verify after power cycle
esp_err_t flasher_swd_flash_firmware(path, cb);      // Full flash + verify
```

**Hardware Configuration:**
- SWDIO: GPIO 0 → Target SWDIO
- SWCLK: GPIO 3 → Target SWCLK
- nRESET: -1 (not connected)

**Flash Strategy:**
- RAM buffer entire FW (max 128KB) → close SD → flash
- 256B sub-chunks with on-the-fly verify + retry
- RDP blind write: 3x attempts with SYSRESETREQ between
- Returns ESP_ERR_INVALID_STATE if chip RDP-locked

### App Actions (`main/app_actions/app_actions.cpp`)
**Purpose:** High-level action dispatcher for flash/erase operations
**Key Logic:**
- Detects STM32 vs ESP32 target from firmware metadata device_type
- Routes to SWD engine (STM32) or UART engine (ESP32)
- Handles ESP_ERR_INVALID_STATE → OLED "Erase STM32 first"

### SD Card Module (`main/sd_card/`)
**Purpose:** SD card management and metadata storage
**API:**
```cpp
esp_err_t sd_mount(int cs_pin);                                  // Mount SD card
esp_err_t sd_unmount();                                          // Unmount SD
esp_err_t sd_load_metadata();                                    // Load index.txt
esp_err_t sd_get_firmware_path(fw_id, out_metadata);            // Query metadata
const char** sd_get_menu_display_items(int& out_count);         // Get menu labels
const char** sd_get_menu_id_items();                            // Get menu IDs
```

**Data Structures:**
- `firmware_metadata_t` - Firmware info struct
- `g_firmware_map` - Global map<string, firmware_metadata_t>
- `g_is_sd_mounted` - Mount status flag

**File Format:** JSON index.txt at SD root
```json
{
  "FW_ID": {
    "device_type": "ESP32-C3",
    "version": "1.0.0",
    "path": "/FW_DIR/app.bin",
    "md5": "...",
    "path_bootloader": "/FW_DIR/boot.bin",
    "md5_bootloader": "...",
    "path_partition": "/FW_DIR/part.bin",
    "md5_partition": "..."
  }
}
```

### OLED Menu Module (`main/oled/`)
**Purpose:** User interface on SSD1306 display
**API:**
```cpp
void menu_init(display, displayItems[], idItems[], len);  // Setup menu
int menu_update();                                        // Poll buttons, returns index
const char* menu_get_id(int index);                      // Get firmware ID
void menu_display_selection(int index);                  // Show selection
void menu_redisplay();                                   // Refresh display
void oled_show_message(line1, line2);                    // Status messages
```

**Button Configuration:**
- BTN_UP: GPIO 21 (scroll up)
- BTN_DOWN: GPIO 20 (scroll down)
- BTN_OK: GPIO 10 (select)

**Display Specs:**
- Resolution: 128x32 pixels
- I2C Address: 0x3C
- SDA: GPIO 8
- SCL: GPIO 9

### WiFi Config Module (`main/wifi_config/`)
**Purpose:** WiFi connection and configuration storage
**API:**
```cpp
bool wifi_config_connect();                              // Connect WiFi (auto or portal)
void wifi_config_force_portal();                         // Force captive portal
void wifi_config_get_params(url, key, iv);              // Read config from SD
void wifi_config_stop();                                 // Disconnect WiFi
```

**Configuration Files (on SD):**
- `/config/url.txt` - Remote firmware URL
- `/config/aes_key.txt` - AES-128 key (16 bytes)
- `/config/aes_iv.txt` - AES-128 IV (16 bytes)

**Defaults:**
- URL: GitHub raw URL format
- Key: "1234567890123456"
- IV: "0000000000000000"

### OTA Downloader Module (`main/ota_downloader/`)
**Purpose:** HTTP download with AES decryption
**API:**
```cpp
string ota_download_index(url, save_path);                    // Download text file
bool ota_download_file_encrypted(url, save_path, key, iv);   // Download & decrypt
```

**Features:**
- HTTP(S) client using esp_http_client
- Streaming AES-128-CBC decryption via mbedtls
- Direct save to SD card
- Progress feedback via OLED

### Metadata Parser Module (`main/metadata_parser/`)
**Purpose:** JSON parsing for firmware catalogs
**API:**
```cpp
bool metadata_parse_json(const String& json_content, FirmwareMap& out_map);
```

**Uses:** ArduinoJson library (bblanchon)
**Input:** JSON string from index.txt
**Output:** Populates FirmwareMap with parsed metadata

### Sync Engine Module (`main/sync_engine/`)
**Purpose:** Orchestrate firmware synchronization
**API:**
```cpp
void sync_engine_run(bool force_clean);
```

**Workflow:**
1. Download remote index.txt
2. Parse remote JSON
3. Load local index from SD
4. Compare versions/hashes
5. Download changed/new encrypted files
6. Decrypt and save to SD
7. Delete obsolete files (if force_clean)
8. Update local index.txt

### Shared Types (`main/firmware_types.h`)
**Purpose:** Common data structures
```cpp
typedef struct {
    string device_type;
    string version;
    string path;
    string md5;
    string path_bootloader;
    string md5_bootloader;
    string path_partition;
    string md5_partition;
} firmware_metadata_t;

using FirmwareMap = map<string, firmware_metadata_t>;
```

## Dependencies

### IDF Component Manager (`main/idf_component.yml`)
```yaml
idf: ">=4.1.0"
espressif/esp-serial-flasher: '*'
espressif/arduino-esp32: '*'
bblanchon/ArduinoJson: '*'
```

### Build Configuration (`main/CMakeLists.txt`)
**Source Files:**
- main.cpp
- sd_card/sd_card.cpp
- flasher/flasher.cpp
- oled/menu.cpp
- wifi_config/wifi_config.cpp
- ota_downloader/ota_downloader.cpp
- metadata_parser/metadata_parser.cpp
- sync_engine/sync_engine.cpp

**Required Components:**
- espressif__arduino-esp32
- Adafruit_GFX
- Adafruit_SSD1306
- WiFiManager
- esp_http_client
- mbedtls

### Target Configuration (`CMakeLists.txt`)
```cmake
SUPPORTED_TARGETS: esp32c3
PROJECT_NAME: ESP_MultiFlasher
```

## Build System

**Framework:** ESP-IDF v5.1.6
**Build Tool:** CMake + Ninja
**Configuration:** sdkconfig (ESP32-C3 specific)

**Build Outputs:**
- `build/ESP_MultiFlasher.bin` - Main firmware
- `build/ESP_MultiFlasher.elf` - Debug symbols
- `build/bootloader/bootloader.bin` - Bootloader
- `build/partition_table/partition-table.bin` - Partition table

**Key Build Options:**
- Arduino component integration enabled
- USB Serial JTAG support (ESP32-C3)
- WiFi/BT enabled
- FreeRTOS task management

## Memory Management Strategy

**Critical Design Decision:** System restarts after each operation
**Rationale:** Prevent memory fragmentation and leaks in long-running operations
**Implementation:** `host_system_restart()` called after flash/sync/monitor/erase

**Implications:**
- No persistent session state
- Menu reloads from SD on each boot
- WiFi credentials managed by WiFiManager (NVS storage)
- Forces clean slate for each operation

## Code Organization Patterns

**Module Structure:**
Each functional module follows pattern:
```
module_name/
├── module_name.h    # Public API declarations
└── module_name.cpp  # Implementation
```

**Header Guards:** Mix of `#pragma once` and traditional `#ifndef`
**Naming Convention:**
- Functions: `module_verb_noun()` (e.g., `sd_load_metadata`)
- Constants: `ALL_CAPS` with prefix
- Types: `lowercase_t` suffix for structs
- Globals: `g_` prefix

**Error Handling:** ESP-IDF style `esp_err_t` return codes
**Logging:** ESP_LOG macros (ESP_LOGI, ESP_LOGE, ESP_LOGW)

## Inter-Module Dependencies

```
main.cpp
├── firmware_types.h (shared types)
├── sd_card → firmware_types.h
├── flasher → sd_card
├── oled (standalone UI)
├── wifi_config → sd_card (for config files)
├── ota_downloader → wifi_config, oled
├── metadata_parser → firmware_types.h
└── sync_engine → all above modules
```

**Dependency Flow:** Bottom-up initialization in setup()
1. OLED (display errors immediately)
2. SD Card (required for operation)
3. Metadata (firmware catalog)
4. Menu (user interface)
5. Modules invoked on-demand from loop()

## Configuration Points

**Hardware Pins:** Defined in module headers (flasher.h, menu.h)
**File Paths:** Defined in wifi_config.h
**Flash Addresses:** Defined in flasher.h
**Buffer Sizes:** Scattered in implementation files
**Timeouts:** Hardcoded delays via vTaskDelay()

## External Tool: FlashPorter (`toolAddFirmware/`)

**FlashPorter** is a Python/Tkinter GUI tool for preparing SD cards and managing firmware encryption.

### Location & Files
```
toolAddFirmware/
├── FlashPorter.py         # Main GUI (~990 lines, Python 3)
├── esp_encryptor.py       # AES-128-CBC encryption class
├── tool_setting.json      # Persistent config (key, IV, SD path, repo URL)
├── firmware_library/      # Local firmware storage (plain .bin)
├── _release_for_git/      # Encrypted output (.enc files + index.txt)
├── dist/FlashPorter.exe   # Windows executable (PyInstaller)
└── icons/logo.png
```

### Features (2 Tabs)
**Tab 1: "Thêm firmware (mới)" - Add New Firmware**
- Input: FW ID, Device Type, Version
- Select 3 binary files: App (FW.bin), Bootloader (BOTL.bin), Partition (PART.bin)
- Auto-calculate MD5 checksums
- Save to `firmware_library/<fw_id>/` with `fw_metadata.json`

**Tab 2: "Quản lý & Đồng bộ SD" - Manage & Sync**
- **Local Library** (left): List/delete firmware from library
- **Actions** (center):
  - Copy to SD Card (Plain) - with PKCS7 padding
  - Encrypt & Export (Local) - creates `.enc` files
  - Remove from SD Card
  - Push to Cloud (GitHub) - auto git init/commit/push
- **SD Card** (right): Browse SD, view index.txt contents
- **Config**: AES Key/IV (16 chars), Git Repo URL

### Key Functions
```python
# esp_encryptor.py
class FWEncryptor:
    def __init__(key_str, iv_str)      # Validate 16/24/32 byte key, 16 byte IV
    def encrypt_data(raw_data) -> bytes # AES-CBC encrypt with PKCS7 padding
    def encrypt_file(input, output)     # File-to-file encryption

# FlashPorter.py
def on_copy_to_sd()       # Copy with padding, update MD5 to padded hash
def on_export_git()       # Encrypt & export to _release_for_git/
def on_push_to_git()      # Generate auto_push.bat, execute git push
def calculate_md5(path)   # MD5 checksum
def safe_name(name)       # Sanitize folder names (lowercase, no spaces)
```

### SD Card Output Format
**Plain (Offline Mode):**
```
SD_ROOT/
├── index.txt              # JSON array of firmware metadata
├── config/
│   ├── url.txt            # Remote server URL
│   ├── aes_key.txt        # AES key (16 bytes)
│   └── aes_iv.txt         # AES IV (16 bytes)
└── <fw_id>/               # 8.3 compliant folder name
    ├── FW.bin             # App binary (PKCS7 padded)
    ├── BOTL.bin           # Bootloader (PKCS7 padded)
    └── PART.bin           # Partition table (PKCS7 padded)
```

**Encrypted (Online Sync):**
```
_release_for_git/
├── index.txt              # JSON with .enc paths & padded MD5
└── <fw_id>/
    ├── FW.enc             # AES-128-CBC encrypted
    ├── BOTL.enc
    └── PART.enc
```

### Metadata JSON Schema
```json
{
  "fw_id": "phuc01",
  "device_type": "ESP32-C3",
  "version": "1.0.0",
  "path": "/phuc01/FW.bin",
  "md5": "abc123...",              // MD5 of PKCS7-padded data
  "path_bootloader": "/phuc01/BOTL.bin",
  "md5_bootloader": "def456...",
  "path_partition": "/phuc01/PART.bin",
  "md5_partition": "ghi789..."
}
```

### Dependencies
- Python 3.x
- pycryptodome (`pip install pycryptodome`)
- tkinter (built-in)
- PyInstaller (for building .exe)

### Build Executable
```bash
cd toolAddFirmware
pip install pycryptodome pyinstaller
pyinstaller FlashPorter.spec
# Output: dist/FlashPorter.exe
```

### Configuration Persistence
Settings saved to `tool_setting.json`:
```json
{
  "key": "1234567890123456",
  "iv": "0000000000005555",
  "sd_path": "D:/",
  "repo_url": "https://github.com/user/repo.git"
}
```

## Testing & Quality Assurance

### Current Test Coverage
- **Unit Tests:** None (no test framework integrated)
- **Integration Tests:** Manual testing on hardware only
- **Static Analysis:** Not configured
- **Code Coverage:** Not measured

### Test Procedure (Manual)
1. Boot test: Power on, verify menu displays
2. SD mount test: Insert/remove SD, check error handling
3. Flash test: Flash known-good firmware to target
4. Sync test: Connect WiFi, download encrypted firmware
5. Monitor test: Verify UART log capture
6. Erase test: Wipe target chip, verify success

### Quality Metrics
- **Build Status:** Clean build with no errors/warnings (except suppressed -Wno-unused-parameter)
- **Code Style:** Mixed (Vietnamese + English comments, inconsistent naming)
- **Documentation:** Comprehensive (README, PDR, architecture docs)

## Unresolved Codebase Questions

1. ~~FlashPorter PC tool source code location and language?~~ **RESOLVED**: `toolAddFirmware/FlashPorter.py` (Python/Tkinter)
2. Error recovery if JSON parse fails mid-sync (rollback to previous index.txt)?
3. Max firmware count limit (menu/memory) - need stress test with 50+ firmwares?
4. UART monitor task cleanup on exit - are there resource leaks?
5. WiFiManager customization parameters - what portal settings are used?
6. Certificate handling for HTTPS downloads - is CA bundle embedded?
7. MD5 verification error handling (retry logic) - current behavior on mismatch?
8. Actual UART baud rate used for flashing (esp-serial-flasher default)?
9. AES decryption error handling if key/IV mismatch?
10. FlashPorter git push uses `--force` - potential data loss risk?

## Performance Benchmarks (Estimated)

**Boot Time:**
- Cold boot to menu: ~2-3 seconds
- Post-restart to menu: ~2 seconds

**Flash Operations:**
- 1MB firmware (bootloader + partition + app): ~30 seconds
- MD5 verification: +2-3 seconds
- Chip erase: ~10 seconds

**Sync Operations:**
- Download 1MB .enc file: ~5-10 seconds (WiFi dependent)
- AES decrypt: Real-time (streaming)
- Write to SD: ~5 seconds

**Menu Responsiveness:**
- Button debounce delay: 300ms
- Long-press detection: 3 seconds
- Menu redraw: <50ms

## Build Artifacts

**Output Files:**
```
build/
├── ESP_MultiFlasher.bin          # Main application binary
├── ESP_MultiFlasher.elf          # Debug symbols
├── ESP_MultiFlasher.map          # Memory map
├── bootloader/bootloader.bin     # ESP32-C3 bootloader
├── partition_table/
│   └── partition-table.bin       # Partition layout
└── compile_commands.json         # Clang tooling support
```

**Flash Command:**
```bash
idf.py -p COM3 flash
# Or manual:
esptool.py --chip esp32c3 --port COM3 \
  write_flash 0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 ESP_MultiFlasher.bin
```

**Binary Sizes (Typical):**
- Bootloader: ~28KB
- Partition Table: ~3KB
- Application: ~800KB-1MB (depends on libraries)

## Version History

**v1.0.0 (2025-11-27):**
- Initial release
- Complete offline and online flash support
- AES-128-CBC encrypted sync
- Monitor mode and chip erase
- Documentation suite

**Previous Commits:**
- 9c778c1 - feat: update source and improve structure
- a61a2d0 - feat: update source and improve structure
- 4cc1902 - Add files via upload
- 3b0dba4 - Update SD structure formatting in README.md
- 69e5347 - Update SD card structure formatting in README
