/**
 * @file prog_common.h
 * @brief Shared definitions for all flasher engines (ESP32 UART, STM32 SWD).
 * @details Pin definitions, mode enum, and shared utility declarations
 *          used by both esp_flasher and swd_flasher modules.
 */

#ifndef __PROG_COMMON_H__
#define __PROG_COMMON_H__

#include "esp_err.h"
#include "../../pin_config.h"   // All pin definitions centralized here

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
 * @brief Lưu UI state và khởi động lại ESP32 Host với animation
 * @param tab Current tab index (0-3: FW/Tools/Desc/Info)
 * @param item_id Current item ID (firmware ID như "F103" hoặc tool index như "0")
 */
void host_system_restart(int tab, const char* item_id);

/**
 * @brief Probe SWD IDCODE to determine target chip family.
 * Quick connect → read DP IDCODE → disconnect. No chip-specific ops.
 * @return IDCODE (0x1BA01477 = Cortex-M3/F1, 0x2BA01477 = Cortex-M4/F4), 0 on fail.
 */
uint32_t swd_probe_idcode(void);

// Well-known SWD IDCODE values
#define SWD_IDCODE_CORTEX_M3    0x1BA01477   // STM32F1xx
#define SWD_IDCODE_CORTEX_M4    0x2BA01477   // STM32F4xx

#endif // __PROG_COMMON_H__
