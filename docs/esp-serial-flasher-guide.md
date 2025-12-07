# 📚 TÀI LIỆU THƯ VIỆN ESP-SERIAL-FLASHER

> Tài liệu tiếng Việt chi tiết về thư viện esp-serial-flasher của Espressif.
> Phiên bản: 2024 | Tác giả: Claude Code Analysis

## Mục Lục
1. [Tổng Quan](#1-tổng-quan)
2. [Kiến Trúc Hệ Thống](#2-kiến-trúc-hệ-thống)
3. [API Công Khai](#3-api-công-khai)
4. [Giao Thức Truyền Thông](#4-giao-thức-truyền-thông)
5. [Chuỗi Lệnh ROM Bootloader](#5-chuỗi-lệnh-rom-bootloader)
6. [Luồng Hoạt Động Chi Tiết](#6-luồng-hoạt-động-chi-tiết)
7. [Port Layer (Abstraction)](#7-port-layer-abstraction)
8. [Cấu Hình](#8-cấu-hình)
9. [Xử Lý Lỗi](#9-xử-lý-lỗi)
10. [Troubleshooting Cho Dự Án](#10-troubleshooting-cho-dự-án)

---

## 1. TỔNG QUAN

### 1.1 Mục Đích
ESP-Serial-Flasher là thư viện C portable cho phép **nạp firmware** vào chip ESP từ các MCU khác (không cần PC/Python). Tương tự `esptool.py` nhưng chạy trên embedded host.

### 1.2 Mô Hình Host-Target

```
┌─────────────────┐                    ┌─────────────────┐
│     HOST        │                    │     TARGET      │
│  (ESP32-C3)     │                    │   (ESP32/S3)    │
│                 │      UART/SPI      │                 │
│  Chạy thư viện  │◄──────────────────►│  ROM Bootloader │
│  esp-serial-    │   TX/RX/EN/IO0     │                 │
│  flasher        │                    │                 │
└─────────────────┘                    └─────────────────┘
```

### 1.3 Chức Năng Chính

| Chức năng | Mô tả |
|-----------|-------|
| **Kết nối** | Handshake với ROM bootloader, detect chip |
| **Flash Write** | Ghi firmware vào flash (bootloader, partition, app) |
| **Flash Read** | Đọc dữ liệu từ flash |
| **Flash Erase** | Xóa toàn bộ hoặc vùng flash |
| **RAM Load** | Tải và chạy binary trong RAM |
| **MD5 Verify** | Xác thực dữ liệu đã ghi |
| **Register R/W** | Đọc/ghi register của target |

### 1.4 Giao Diện Hỗ Trợ

| Interface | Flash | RAM | Ghi chú |
|-----------|-------|-----|---------|
| UART | ✅ | ✅ | Phổ biến nhất |
| USB CDC ACM | ✅ | ✅ | Qua USB |
| SPI | ❌ | ✅ | Chỉ RAM |
| SDIO | ✅ | ✅ | Thử nghiệm |

### 1.5 Target Hỗ Trợ

ESP8266, ESP32, ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-H2, ESP32-P4

---

## 2. KIẾN TRÚC HỆ THỐNG

### 2.1 Cấu Trúc Thư Mục

```
esp-serial-flasher/
├── include/                    # Header công khai
│   ├── esp_loader.h           # API chính
│   └── esp_loader_io.h        # Interface I/O (port phải implement)
├── src/                        # Source code
│   ├── esp_loader.c           # Logic chính
│   ├── protocol_uart.c        # Protocol layer cho UART
│   ├── protocol_serial.c      # Lệnh serial bootloader
│   ├── slip.c                 # SLIP encoding/decoding
│   ├── esp_targets.c          # Thông tin chip target
│   └── esp_stubs.c            # Flasher stub data
├── port/                       # Hardware abstraction
│   ├── esp32_port.c           # Port cho ESP-IDF
│   ├── stm32_port.c           # Port cho STM32
│   └── ...
└── private_include/            # Header nội bộ
    ├── protocol.h             # Định nghĩa command/response
    └── slip.h                 # SLIP protocol
```

### 2.2 Kiến Trúc Layer

```
┌────────────────────────────────────────────────────────────┐
│                    USER APPLICATION                         │
│         (flasher.cpp - code của bạn)                       │
└────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────┐
│                  PUBLIC API LAYER                           │
│   esp_loader.h: esp_loader_connect(), flash_start(), ...   │
└────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────┐
│                  PROTOCOL LAYER                             │
│   protocol_serial.c: loader_sync_cmd(), flash_begin_cmd()  │
│   protocol_uart.c: loader_initialize_conn(), send_cmd()    │
└────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────┐
│                    SLIP LAYER                               │
│   slip.c: SLIP_send(), SLIP_receive_packet()               │
│   Encoding: 0xC0 → 0xDB 0xDC | 0xDB → 0xDB 0xDD           │
└────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────┐
│                    PORT LAYER                               │
│   esp32_port.c: loader_port_write(), loader_port_read()    │
│   loader_port_enter_bootloader(), loader_port_reset()      │
└────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────┐
│                    HARDWARE                                 │
│   UART Driver (uart_write_bytes, uart_read_bytes)          │
│   GPIO (EN pin, IO0/BOOT pin)                              │
└────────────────────────────────────────────────────────────┘
```

---

## 3. API CÔNG KHAI

### 3.1 Cấu Trúc Dữ Liệu

```c
// Mã lỗi
typedef enum {
    ESP_LOADER_SUCCESS,              // Thành công
    ESP_LOADER_ERROR_FAIL,           // Lỗi chung
    ESP_LOADER_ERROR_TIMEOUT,        // Hết thời gian chờ
    ESP_LOADER_ERROR_IMAGE_SIZE,     // Image quá lớn
    ESP_LOADER_ERROR_INVALID_MD5,    // MD5 không khớp
    ESP_LOADER_ERROR_INVALID_PARAM,  // Tham số không hợp lệ
    ESP_LOADER_ERROR_INVALID_TARGET, // Target không hợp lệ
    ESP_LOADER_ERROR_UNSUPPORTED_CHIP,
    ESP_LOADER_ERROR_UNSUPPORTED_FUNC,
    ESP_LOADER_ERROR_INVALID_RESPONSE
} esp_loader_error_t;

// Loại chip target
typedef enum {
    ESP8266_CHIP = 0,
    ESP32_CHIP   = 1,
    ESP32S2_CHIP = 2,
    ESP32C3_CHIP = 3,
    ESP32S3_CHIP = 4,
    // ...
} target_chip_t;

// Tham số kết nối
typedef struct {
    uint32_t sync_timeout;  // Timeout chờ sync (ms)
    int32_t trials;         // Số lần thử kết nối
} esp_loader_connect_args_t;

// Giá trị mặc định
#define ESP_LOADER_CONNECT_DEFAULT() { \
    .sync_timeout = 100, \
    .trials = 10, \
}
```

### 3.2 Các Hàm Chính

#### Kết Nối

```c
// Kết nối với target (ROM bootloader)
esp_loader_error_t esp_loader_connect(esp_loader_connect_args_t *connect_args);

// Kết nối với flasher stub (nhanh hơn)
esp_loader_error_t esp_loader_connect_with_stub(esp_loader_connect_args_t *connect_args);

// Lấy loại chip đã kết nối
target_chip_t esp_loader_get_target(void);
```

#### Flash Operations

```c
// Bắt đầu ghi flash
esp_loader_error_t esp_loader_flash_start(
    uint32_t offset,      // Địa chỉ bắt đầu (0x1000, 0x8000, 0x10000)
    uint32_t image_size,  // Kích thước image
    uint32_t block_size   // Kích thước mỗi block ghi
);

// Ghi dữ liệu vào flash
esp_loader_error_t esp_loader_flash_write(void *payload, uint32_t size);

// Kết thúc ghi flash
esp_loader_error_t esp_loader_flash_finish(bool reboot);

// Xóa toàn bộ flash
esp_loader_error_t esp_loader_flash_erase(void);

// Xóa vùng flash
esp_loader_error_t esp_loader_flash_erase_region(uint32_t offset, uint32_t size);

// Đọc flash
esp_loader_error_t esp_loader_flash_read(uint8_t *buf, uint32_t address, uint32_t length);

// Detect kích thước flash
esp_loader_error_t esp_loader_flash_detect_size(uint32_t *flash_size);
```

#### RAM Operations

```c
// Tải binary vào RAM
esp_loader_error_t esp_loader_mem_start(uint32_t offset, uint32_t size, uint32_t block_size);
esp_loader_error_t esp_loader_mem_write(const void *payload, uint32_t size);
esp_loader_error_t esp_loader_mem_finish(uint32_t entrypoint);
```

#### Utilities

```c
// Thay đổi baudrate
esp_loader_error_t esp_loader_change_transmission_rate(uint32_t transmission_rate);

// Đọc/ghi register
esp_loader_error_t esp_loader_read_register(uint32_t address, uint32_t *reg_value);
esp_loader_error_t esp_loader_write_register(uint32_t address, uint32_t reg_value);

// Đọc MAC address
esp_loader_error_t esp_loader_read_mac(uint8_t *mac);

// Reset target
void esp_loader_reset_target(void);

// Verify MD5
esp_loader_error_t esp_loader_flash_verify(void);
esp_loader_error_t esp_loader_flash_verify_known_md5(uint32_t address, uint32_t size, const uint8_t *expected_md5);
```

---

## 4. GIAO THỨC TRUYỀN THÔNG

### 4.1 SLIP Protocol (Serial Line Internet Protocol)

SLIP dùng để đóng gói frame dữ liệu trên serial:

```
Frame Format:
┌──────────┬──────────────────────────────┬──────────┐
│ 0xC0     │         DATA (escaped)       │   0xC0   │
│ (START)  │                              │  (END)   │
└──────────┴──────────────────────────────┴──────────┘

Escape Sequences:
- 0xC0 trong data → 0xDB 0xDC
- 0xDB trong data → 0xDB 0xDD
```

**Code SLIP:**

```c
// slip.c
static const uint8_t DELIMITER = 0xC0;
static const uint8_t C0_REPLACEMENT[2] = {0xDB, 0xDC};
static const uint8_t DB_REPLACEMENT[2] = {0xDB, 0xDD};

// Gửi frame
esp_loader_error_t SLIP_send(const uint8_t *data, size_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (data[i] == 0xC0) {
            peripheral_write(C0_REPLACEMENT, 2);
        } else if (data[i] == 0xDB) {
            peripheral_write(DB_REPLACEMENT, 2);
        } else {
            peripheral_write(&data[i], 1);
        }
    }
}

// Nhận frame
esp_loader_error_t SLIP_receive_packet(uint8_t *buff, size_t max_size, size_t *recv_size) {
    // 1. Chờ delimiter 0xC0
    do { peripheral_read(&ch, 1); } while (ch != DELIMITER);

    // 2. Đọc data, decode escape sequences
    for (size_t i = 0; i < max_size; i++) {
        peripheral_read(&ch, 1);
        if (ch == 0xDB) {
            peripheral_read(&ch, 1);
            buff[i] = (ch == 0xDC) ? 0xC0 : 0xDB;
        } else if (ch == DELIMITER) {
            *recv_size = i;
            return SUCCESS;
        } else {
            buff[i] = ch;
        }
    }
}
```

### 4.2 Command/Response Format

#### Command Structure (Host → Target)

```c
typedef struct __attribute__((packed)) {
    uint8_t direction;   // 0x00 = WRITE (command)
    uint8_t command;     // Command ID (SYNC=0x08, FLASH_BEGIN=0x02,...)
    uint16_t size;       // Kích thước payload
    uint32_t checksum;   // XOR checksum của data
} command_common_t;
```

#### Response Structure (Target → Host)

```c
typedef struct __attribute__((packed)) {
    uint8_t direction;   // 0x01 = READ (response)
    uint8_t command;     // Echo lại command ID
    uint16_t size;       // Kích thước response
    uint32_t value;      // Giá trị trả về (register value, etc.)
} common_response_t;

typedef struct __attribute__((packed)) {
    uint8_t failed;      // 0 = success, 1 = failed
    uint8_t error;       // Error code nếu failed
} response_status_t;
```

---

## 5. CHUỖI LỆNH ROM BOOTLOADER

### 5.1 Danh Sách Commands

| Command | ID | Mô tả |
|---------|-----|-------|
| SYNC | 0x08 | Đồng bộ với bootloader |
| FLASH_BEGIN | 0x02 | Bắt đầu ghi flash |
| FLASH_DATA | 0x03 | Ghi block data |
| FLASH_END | 0x04 | Kết thúc ghi flash |
| MEM_BEGIN | 0x05 | Bắt đầu tải RAM |
| MEM_DATA | 0x07 | Ghi data vào RAM |
| MEM_END | 0x06 | Chạy code từ RAM |
| READ_REG | 0x0A | Đọc register |
| WRITE_REG | 0x09 | Ghi register |
| SPI_ATTACH | 0x0D | Attach SPI flash |
| CHANGE_BAUDRATE | 0x0F | Thay đổi baudrate |
| SPI_FLASH_MD5 | 0x13 | Tính MD5 của vùng flash |
| ERASE_FLASH | 0xD0 | Xóa toàn bộ flash (stub) |

### 5.2 SYNC Command

```c
// protocol_serial.c
typedef struct __attribute__((packed)) {
    command_common_t common;
    uint8_t sync_sequence[36];  // Magic sequence
} sync_command_t;

esp_loader_error_t loader_sync_cmd(void) {
    sync_command_t sync_cmd = {
        .common = {
            .direction = WRITE_DIRECTION,
            .command = SYNC,  // 0x08
            .size = 36,
            .checksum = 0
        },
        .sync_sequence = {
            0x07, 0x07, 0x12, 0x20,  // Header
            0x55, 0x55, 0x55, 0x55,  // 32 bytes của 0x55
            0x55, 0x55, 0x55, 0x55,
            // ...
        }
    };
    return send_cmd(&sync_cmd);
}
```

**SYNC sequence = `07 07 12 20` + 32 bytes `0x55`**

ROM bootloader nhận được SYNC sẽ trả về **8 response** (để flush buffer).

### 5.3 FLASH_BEGIN Command

```c
typedef struct __attribute__((packed)) {
    command_common_t common;
    uint32_t erase_size;     // Số byte cần xóa
    uint32_t packet_count;   // Số block sẽ gửi
    uint32_t packet_size;    // Kích thước mỗi block
    uint32_t offset;         // Địa chỉ flash bắt đầu
    uint32_t encrypted;      // Flash encryption flag
} flash_begin_command_t;
```

### 5.4 FLASH_DATA Command

```c
typedef struct __attribute__((packed)) {
    command_common_t common;
    uint32_t data_size;        // Kích thước data
    uint32_t sequence_number;  // Số thứ tự block (0, 1, 2,...)
    uint32_t zero_0;
    uint32_t zero_1;
    // Theo sau là data thực tế
} data_command_t;
```

**Checksum = XOR của toàn bộ data bytes, bắt đầu với 0xEF**

```c
static uint8_t compute_checksum(const uint8_t *data, uint32_t size) {
    uint8_t checksum = 0xEF;
    while (size--) {
        checksum ^= *data++;
    }
    return checksum;
}
```

---

## 6. LUỒNG HOẠT ĐỘNG CHI TIẾT

### 6.1 Luồng esp_loader_connect()

```
esp_loader_connect()
│
├── 1. loader_port_enter_bootloader()     ← VÀO BOOTLOADER MODE
│       ├── IO0 = LOW (Boot mode)
│       ├── EN = LOW → delay → EN = HIGH  (Reset pulse)
│       └── delay → IO0 = HIGH
│
├── 2. loader_initialize_conn()           ← SYNC VỚI TARGET
│       └── loop (trials):
│           ├── loader_sync_cmd()
│           │   ├── SLIP_send(SYNC command)
│           │   └── SLIP_receive(8 responses)
│           └── if timeout → delay 100ms → retry
│
├── 3. loader_detect_chip()               ← XÁC ĐỊNH LOẠI CHIP
│       ├── Đọc magic register
│       └── Map ID → target_chip_t
│
└── 4. loader_spi_attach_cmd()            ← ATTACH SPI FLASH
        └── Gửi SPI_ATTACH command
```

### 6.2 Luồng Ghi Flash

```
flasher_begin_session("fw_id")
│
├── 1. sd_get_firmware_path()              ← Lấy metadata từ SD
│
├── 2. esp_loader_connect()                ← Kết nối target
│
├── 3. esp_loader_change_transmission_rate() ← Boost baudrate
│       ├── Gửi CHANGE_BAUDRATE command
│       └── Host thay đổi UART baudrate
│
├── 4. flasher_write_segment() x3          ← Ghi 3 phân vùng
│       │
│       │   [Cho mỗi segment: bootloader, partition, app]
│       │
│       ├── esp_loader_flash_start(offset, size, block_size)
│       │   └── loader_flash_begin_cmd()
│       │       ├── Tính erase_size
│       │       └── Gửi FLASH_BEGIN command (Target xóa flash)
│       │
│       ├── loop: esp_loader_flash_write(buffer, size)
│       │   └── loader_flash_data_cmd()
│       │       ├── Compute checksum
│       │       └── Gửi FLASH_DATA command + data
│       │
│       └── esp_loader_flash_verify_known_md5()
│           └── loader_md5_cmd()
│               └── So sánh MD5 từ target với expected
│
└── 5. esp_loader_reset_target()           ← Reset target chạy app
        └── loader_port_reset_target()
```

### 6.3 Sequence Diagram Chi Tiết

```
    HOST                           TARGET
      │                               │
      │──── [SLIP: SYNC command] ────►│
      │                               │ (ROM bootloader nhận SYNC)
      │◄─── [SLIP: 8 responses] ──────│
      │                               │
      │──── [SLIP: READ_REG] ────────►│
      │◄─── [chip_id value] ──────────│ (Detect chip type)
      │                               │
      │──── [SLIP: SPI_ATTACH] ──────►│
      │◄─── [OK] ─────────────────────│
      │                               │
      │──── [CHANGE_BAUDRATE] ───────►│
      │◄─── [OK] ─────────────────────│
      │     (Both switch to 921600)   │
      │                               │
      │──── [FLASH_BEGIN] ───────────►│
      │     offset=0x1000             │ (Erase bootloader region)
      │     size=0x7000               │
      │◄─── [OK] ─────────────────────│
      │                               │
      │──── [FLASH_DATA seq=0] ──────►│
      │     4096 bytes                │
      │◄─── [OK] ─────────────────────│
      │                               │
      │──── [FLASH_DATA seq=1] ──────►│
      │     4096 bytes                │
      │◄─── [OK] ─────────────────────│
      │          ...                  │
      │                               │
      │──── [SPI_FLASH_MD5] ─────────►│
      │◄─── [32-char MD5] ────────────│ (Verify)
      │                               │
      │     (Repeat for partition     │
      │      and app segments)        │
      │                               │
      │──── [EN pulse] ──────────────►│ (Reset to run app)
      │                               │
```

---

## 7. PORT LAYER (ABSTRACTION)

### 7.1 Interface Cần Implement

```c
// esp_loader_io.h - Các hàm PORT PHẢI implement

// Ghi data ra UART
esp_loader_error_t loader_port_write(const uint8_t *data, uint16_t size, uint32_t timeout);

// Đọc data từ UART
esp_loader_error_t loader_port_read(uint8_t *data, uint16_t size, uint32_t timeout);

// Delay
void loader_port_delay_ms(uint32_t ms);

// Timer management
void loader_port_start_timer(uint32_t ms);
uint32_t loader_port_remaining_time(void);

// ==== QUAN TRỌNG NHẤT ====
// Đưa target vào bootloader mode
void loader_port_enter_bootloader(void);

// Reset target
void loader_port_reset_target(void);

// Debug print
void loader_port_debug_print(const char *str);
```

### 7.2 Implement ESP32 Port (esp32_port.c)

```c
// Biến static lưu cấu hình
static int32_t s_uart_port;
static int32_t s_reset_trigger_pin;
static int32_t s_gpio0_trigger_pin;

// Khởi tạo
esp_loader_error_t loader_port_esp32_init(const loader_esp32_config_t *config) {
    s_uart_port = config->uart_port;
    s_reset_trigger_pin = config->reset_trigger_pin;
    s_gpio0_trigger_pin = config->gpio0_trigger_pin;

    // Setup UART
    uart_param_config(s_uart_port, &uart_config);
    uart_driver_install(s_uart_port, rx_buffer, tx_buffer, ...);

    // Setup GPIO
    gpio_reset_pin(s_reset_trigger_pin);
    gpio_set_direction(s_reset_trigger_pin, GPIO_MODE_OUTPUT);
    gpio_reset_pin(s_gpio0_trigger_pin);
    gpio_set_direction(s_gpio0_trigger_pin, GPIO_MODE_OUTPUT);
}

// ===== CRITICAL: Enter Bootloader =====
void loader_port_enter_bootloader(void) {
    // 1. IO0 = LOW (Boot mode)
    gpio_set_level(s_gpio0_trigger_pin, 0);

    // 2. Reset pulse
    loader_port_reset_target();

    // 3. Giữ IO0 LOW một lúc
    loader_port_delay_ms(SERIAL_FLASHER_BOOT_HOLD_TIME_MS);  // 50ms mặc định

    // 4. Thả IO0
    gpio_set_level(s_gpio0_trigger_pin, 1);
}

void loader_port_reset_target(void) {
    // EN = LOW (Reset)
    gpio_set_level(s_reset_trigger_pin, 0);
    loader_port_delay_ms(SERIAL_FLASHER_RESET_HOLD_TIME_MS);  // 100ms mặc định
    // EN = HIGH (Release)
    gpio_set_level(s_reset_trigger_pin, 1);
}

// UART Write
esp_loader_error_t loader_port_write(const uint8_t *data, uint16_t size, uint32_t timeout) {
    uart_write_bytes(s_uart_port, data, size);
    return uart_wait_tx_done(s_uart_port, pdMS_TO_TICKS(timeout));
}

// UART Read
esp_loader_error_t loader_port_read(uint8_t *data, uint16_t size, uint32_t timeout) {
    int read = uart_read_bytes(s_uart_port, data, size, pdMS_TO_TICKS(timeout));
    return (read == size) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_TIMEOUT;
}
```

### 7.3 Vấn Đề Với Mạch Autoboot (DTR/RTS)

Code dự án dùng **mạch autoboot** với transistor, cần logic **KHÁC**:

```
Mạch Autoboot (NodeMCU style):
┌─────────────────────────────────────────┐
│                                         │
│   DTR ──┬──[Q1]──► EN                  │
│         │                               │
│   RTS ──┼──[Q2]──► IO0                 │
│         │                               │
│   DTR ──┴──[Q2]──► (cross)             │
│   RTS ──────[Q1]──► (cross)            │
│                                         │
└─────────────────────────────────────────┘

Logic đan chéo:
│ DTR │ RTS │  EN  │ IO0  │
│  0  │  1  │ LOW  │ HIGH │  → Reset chip
│  1  │  0  │ HIGH │ LOW  │  → Enter bootloader
│  1  │  1  │ HIGH │ HIGH │  → Normal run
```

**Thư viện mặc định dùng DIRECT GPIO** (không phải DTR/RTS toggle), nên code custom `reset_sequence()` trong dự án là cần thiết.

---

## 8. CẤU HÌNH

### 8.1 CMake Variables

| Variable | Default | Mô tả |
|----------|---------|-------|
| `SERIAL_FLASHER_INTERFACE_UART` | ON | Dùng UART |
| `SERIAL_FLASHER_INTERFACE_SPI` | OFF | Dùng SPI |
| `MD5_ENABLED` | ON (ESP-IDF) | Bật MD5 verify |
| `SERIAL_FLASHER_WRITE_BLOCK_RETRIES` | 3 | Số lần retry |
| `SERIAL_FLASHER_RESET_HOLD_TIME_MS` | 100 | Thời gian giữ reset |
| `SERIAL_FLASHER_BOOT_HOLD_TIME_MS` | 50 | Thời gian giữ boot |
| `SERIAL_FLASHER_RESET_INVERT` | OFF | Đảo logic reset |
| `SERIAL_FLASHER_BOOT_INVERT` | OFF | Đảo logic boot |

### 8.2 Kconfig (ESP-IDF)

```
idf.py menuconfig
→ Component config → ESP Serial Flasher Configuration

CONFIG_SERIAL_FLASHER_MD5_ENABLED=y
CONFIG_SERIAL_FLASHER_DEBUG_TRACE=n
```

---

## 9. XỬ LÝ LỖI

### 9.1 Error Codes từ ROM Bootloader

| Code | Tên | Nguyên nhân |
|------|-----|-------------|
| 0x05 | INVALID_COMMAND | Command không hợp lệ |
| 0x06 | COMMAND_FAILED | Thực thi thất bại |
| 0x07 | INVALID_CRC | Checksum sai |
| 0x08 | FLASH_WRITE_ERR | Ghi flash thất bại |
| 0x09 | FLASH_READ_ERR | Đọc flash thất bại |
| 0x0B | DEFLATE_ERROR | Giải nén thất bại |

### 9.2 Stub-specific Errors

| Code | Tên | Nguyên nhân |
|------|-----|-------------|
| 0xC0 | BAD_DATA_LEN | Sai kích thước data |
| 0xC1 | BAD_DATA_CHECKSUM | Sai checksum |
| 0xC4 | FAILED_SPI_OP | Thao tác SPI thất bại |

### 9.3 Debug Tips

```c
// Bật debug trace trong CMake
cmake -DSERIAL_FLASHER_DEBUG_TRACE=1 ..

// Implement debug print
void loader_port_debug_print(const char *str) {
    ESP_LOGI("FLASHER", "%s", str);
}
```

---

## 10. TROUBLESHOOTING CHO DỰ ÁN

### 10.1 Vấn Đề: "Index 0 OK, Index khác FAIL"

#### Root Cause

```
Luồng thực tế khi try_reset_sequence():

Vòng lặp i=0:
├── reset_sequence(profile=0)     ← Reset #1
├── esp_loader_connect()
│   └── loader_port_enter_bootloader()  ← Reset #2 (từ thư viện)
└── loader_sync_cmd() → SUCCESS (timing trùng hợp OK)

Vòng lặp i=1:
├── Target đang ở BOOTLOADER từ lần trước
├── reset_sequence(profile=1)     ← Reset #1 (nhưng UART còn rác)
├── esp_loader_connect()
│   └── loader_port_enter_bootloader()  ← Reset #2
└── loader_sync_cmd() → FAIL (đọc rác + timing sai)
```

#### Nguyên Nhân Chi Tiết

1. **Double Reset Bug:**
   - `reset_sequence()` reset target lần 1
   - `esp_loader_connect()` gọi `loader_port_enter_bootloader()` reset LẦN NỮA
   - Target có thể boot vào app mode thay vì bootloader

2. **UART Buffer Pollution:**
   - Boot log của target làm rác buffer
   - `loader_sync_cmd()` đọc nhầm → TIMEOUT

3. **Timing Profile Không Được Dùng:**
   - `timing_profile.reset_hold` và `timing_profile.boot_wait` được assign nhưng không sử dụng trong code

### 10.2 Giải Pháp Đề Xuất

```c
static esp_err_t try_reset_sequence_fixed() {
    for (uint8_t i = 0; i < NUM_PROFILES; i++) {
        ESP_LOGI(TAG, "Trying profile: %s", TIMING_PROFILES[i].name);

        // === CRITICAL: Clean state trước mỗi attempt ===

        // 1. Reset UART hoàn toàn
        uart_flush_input(UART_NUM_1);
        uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(100));

        // 2. Đưa GPIO về trạng thái IDLE (cả 2 HIGH)
        gpio_set_level((gpio_num_t)config.gpio0_trigger_pin, 1);
        gpio_set_level((gpio_num_t)config.reset_trigger_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(100));  // Chờ capacitor discharge

        // 3. Thực hiện reset sequence với profile i
        reset_sequence(&config, i);

        // 4. Flush UART SAU reset (xóa boot log)
        vTaskDelay(pdMS_TO_TICKS(50));
        uart_flush_input(UART_NUM_1);

        // 5. KHÔNG dùng esp_loader_connect() vì nó gọi enter_bootloader lần nữa
        //    Thay vào đó, gọi trực tiếp loader_initialize_conn()
        esp_loader_connect_args_t connect_config = ESP_LOADER_CONNECT_DEFAULT();
        connect_config.sync_timeout = 1000;

        // Gọi sync trực tiếp thay vì qua esp_loader_connect()
        loader_port_start_timer(connect_config.sync_timeout);
        esp_loader_error_t err = loader_sync_cmd();

        if (err == ESP_LOADER_SUCCESS) {
            ESP_LOGI(TAG, "Connected with profile: %s", TIMING_PROFILES[i].name);

            // Tiếp tục detect chip và attach SPI
            // loader_detect_chip(...);
            // loader_spi_attach_cmd(...);

            return ESP_OK;
        }

        ESP_LOGW(TAG, "Profile %d failed, retrying...", i);
        vTaskDelay(pdMS_TO_TICKS(500));  // Cool-down giữa các attempt
    }

    return ESP_FAIL;
}
```

### 10.3 Checklist Debug

- [ ] Flush UART trước và sau mỗi reset sequence
- [ ] Không gọi `esp_loader_connect()` sau custom `reset_sequence()` (vì sẽ double reset)
- [ ] Thêm delay giữa các profile attempt
- [ ] Kiểm tra timing_profile có được sử dụng trong reset_sequence() không
- [ ] Đo oscilloscope để verify waveform EN và IO0

---

## Tham Khảo

- [ESP-Serial-Flasher GitHub](https://github.com/espressif/esp-serial-flasher)
- [esptool Documentation](https://docs.espressif.com/projects/esptool/en/latest/)
- [ESP32 Technical Reference Manual - Boot Mode](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
