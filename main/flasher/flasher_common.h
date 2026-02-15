/**
 * @file flasher_common.h
 * @brief Shared definitions for all flasher engines (ESP32 UART, STM32 SWD).
 * @details Pin definitions, mode enum, and shared utility declarations
 *          used by both esp_flasher and swd_flasher modules.
 */

#ifndef __FLASHER_COMMON_H__
#define __FLASHER_COMMON_H__

#include "esp_err.h"
#include "driver/gpio.h"

// ============================================================
// SHARED PIN DEFINITIONS
// ============================================================
// Các chân này được dùng chung (dual-purpose) giữa 2 mode:
//   - ESP32 UART mode: TX/RX + RESET/BOOT triggers
//   - STM32 SWD mode:  SWDIO/SWCLK + nRESET

#define FLASH_PIN_0     GPIO_NUM_0   // ESP: UART TX      | SWD: SWDIO
#define FLASH_PIN_1     GPIO_NUM_1   // ESP: UART RX      | SWD: (free)
#define FLASH_PIN_2     GPIO_NUM_2   // ESP: RESET trigger | SWD: (free)
#define FLASH_PIN_3     GPIO_NUM_3   // ESP: BOOT trigger  | SWD: SWCLK

// ============================================================
// ESP32 UART MODE PIN ALIASES
// ============================================================
#define FLASH_UART_TX_PIN      FLASH_PIN_0
#define FLASH_UART_RX_PIN      FLASH_PIN_1
#define FLASH_RESET_PIN        FLASH_PIN_2
#define FLASH_BOOT_PIN         FLASH_PIN_3

// ============================================================
// STM32 SWD MODE PIN ALIASES
// ============================================================
#define SWD_SWDIO_PIN          FLASH_PIN_0           // GPIO0 (avoid GPIO2 = FSPIQ default)
#define SWD_SWCLK_PIN          FLASH_PIN_3
#define SWD_NRESET_PIN         (-1)                  // Không nối, dùng nút reset vật lý trên mạch

// ============================================================
// COMMON CONSTANTS
// ============================================================
#define FLASHER_BUFFER_SIZE    4096   // Kích thước buffer đọc/ghi (4KB)

// ============================================================
// FLASHER MODE ENUM
// ============================================================
typedef enum {
    FLASH_MODE_NONE = 0,       // Chưa init, tất cả pin ở trạng thái mặc định
    FLASH_MODE_ESP_UART,       // ESP32 flashing qua UART (esp-serial-flasher)
    FLASH_MODE_STM32_SWD,      // STM32 flashing qua SWD (Adafruit_DAP)
} flasher_mode_t;

/**
 * @brief Lấy mode hiện tại của flasher.
 * @return Mode đang active.
 */
flasher_mode_t flasher_get_current_mode(void);

// ============================================================
// SHARED UTILITY FUNCTIONS
// ============================================================

/**
 * @brief Hiển thị thông báo và khởi động lại ESP32 Host.
 */
void host_system_restart(void);

#endif // __FLASHER_COMMON_H__
