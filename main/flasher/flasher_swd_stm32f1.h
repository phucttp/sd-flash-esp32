/**
 * @file flasher_swd_stm32f1.h
 * @brief STM32F1 SWD flasher engine using Adafruit_DAP_STM32F1.
 *
 * API mirrors flasher_swd_stm32f4.h for consistency.
 * Key F1 differences:
 *   - RDP detect via FLASH_OBR (direct read, no guessing)
 *   - RDP disable: standard unlock flow (no blind writes)
 *   - OB reload via SYSRESETREQ (no power cycle required)
 *   - Flash programming in half-words (CSW 16-bit)
 */

#ifndef __FLASHER_SWD_STM32F1_H__
#define __FLASHER_SWD_STM32F1_H__

#include "flasher_common.h"
#include <string>

esp_err_t flasher_swd_stm32f1_init(void);
esp_err_t flasher_swd_stm32f1_deinit(void);

/**
 * @brief Detect STM32F1 RDP level via SWD.
 * Self-contained: init → connect → read FLASH_OBR → disconnect → deinit.
 * @param[out] rdp_level  0 = unprotected, 1 = Level 1. NULL = don't care.
 * @return ESP_OK if detection succeeded.
 */
esp_err_t flasher_swd_stm32f1_detect_rdp(int *rdp_level);

/**
 * @brief Trigger RDP disable (Level 1 → Level 0).
 * Standard unlock: KEYR → OPTKEYR → erase OB → write 0xA5 → wait mass erase → SYSRESETREQ.
 * Unlike F4, peripheral reads work under RDP1 → no blind writes needed.
 * @return ESP_OK if mass erase completed and reset issued.
 */
esp_err_t flasher_swd_stm32f1_rdp_disable_trigger(void);

/**
 * @brief Verify RDP is now Level 0 after reset.
 * Reconnects, reads FLASH_OBR, checks flash is erased.
 * Unlike F4, no power cycle needed — SYSRESETREQ reloads OB on F1.
 * @return ESP_OK if Level 0 confirmed.
 */
esp_err_t flasher_swd_stm32f1_rdp_disable_verify(void);

// ============================================================
// FLASH PROGRAMMING
// ============================================================

typedef void (*flasher_swd_stm32f1_progress_cb_t)(const char* text, int percent);

/**
 * @brief Flash firmware to STM32F1 via SWD.
 * Self-contained: init → warm-up → connect → RDP check → erase → program → verify → deinit.
 * Uses half-word programming (CSW 16-bit toggle inside library programBlock).
 * @param fw_path     Path to .bin file on SD card.
 * @param on_progress Optional progress callback (text, 0-100).
 * @return ESP_OK if flash + verify succeeded.
 */
esp_err_t flasher_swd_stm32f1_flash_firmware(const std::string& fw_path,
                                       flasher_swd_stm32f1_progress_cb_t on_progress);

#endif // __FLASHER_SWD_STM32F1_H__
