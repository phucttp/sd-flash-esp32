/**
 * @file pin_config.h
 * @brief Cấu hình chân phần cứng tập trung cho CHIVI FlashPorter (ESP32-S3).
 *
 * Tất cả GPIO assignments nằm tại đây để dễ dàng thay đổi khi đổi board/layout.
 * Các module khác chỉ cần #include "pin_config.h" thay vì tự define.
 */

#ifndef __PIN_CONFIG_H__
#define __PIN_CONFIG_H__

#include "driver/gpio.h"

// ============================================================
// SD CARD (SPI)
// ============================================================
#define SD_CS_PIN       GPIO_NUM_7

// ============================================================
// TFT DISPLAY — ST7735 (Hardware SPI)
// ============================================================
#define TFT_MOSI        34
#define TFT_SCLK        33
#define TFT_CS          -1      // Khong dung CS (1 device tren bus)
#define TFT_RST         35
#define TFT_DC          36
#define TFT_BL          37      // Backlight qua MOSFET

// ============================================================
// BUTTONS (3-button navigation)
// ============================================================
#define BTN_UP          GPIO_NUM_4
#define BTN_DOWN        GPIO_NUM_1
#define BTN_OK          GPIO_NUM_2

// ============================================================
// FLASHER INTERFACE — Dual-purpose pins
// ============================================================
// Các chân này được dùng chung giữa 2 mode:
//   - ESP32 UART mode: TX/RX + RESET/BOOT triggers
//   - STM32 SWD mode:  SWDIO/SWCLK + nRESET
#define FLASH_PIN_0     GPIO_NUM_13   // ESP: UART TX      | SWD: SWDIO
#define FLASH_PIN_1     GPIO_NUM_14   // ESP: UART RX      | SWD: (free)
#define FLASH_PIN_2     GPIO_NUM_12   // ESP: RESET trigger | SWD: (free)
#define FLASH_PIN_3     GPIO_NUM_11   // ESP: BOOT trigger  | SWD: SWCLK

// --- ESP32 UART mode aliases ---
#define FLASH_UART_TX_PIN      FLASH_PIN_0
#define FLASH_UART_RX_PIN      FLASH_PIN_1
#define FLASH_RESET_PIN        FLASH_PIN_2
#define FLASH_BOOT_PIN         FLASH_PIN_3

// --- STM32 SWD mode aliases ---
#define SWD_SWDIO_PIN          FLASH_PIN_0           // GPIO0 (avoid GPIO2 = FSPIQ default)
#define SWD_SWCLK_PIN          FLASH_PIN_3
#define SWD_NRESET_PIN         (-1)                  // Khong noi, dung nut reset vat ly tren mach

#endif // __PIN_CONFIG_H__
