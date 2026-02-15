/**
 * @file flasher_swd.h
 * @brief STM32 SWD flasher engine using Adafruit_DAP library.
 */

#ifndef __FLASHER_SWD_H__
#define __FLASHER_SWD_H__

#include "flasher_common.h"
#include <string>

esp_err_t flasher_swd_init(void);
esp_err_t flasher_swd_deinit(void);

/**
 * @brief Detect STM32 RDP protection level via SWD.
 * Self-contained: init → connect → read OPTCR → disconnect → deinit.
 * @param[out] rdp_level  Returns RDP level (0, 1, or 2). NULL = don't care.
 * @return ESP_OK if detection succeeded.
 */
esp_err_t flasher_swd_detect_rdp(int *rdp_level);

/**
 * @brief PHASE 1: Trigger RDP disable (Level 1 → Level 0).
 * Connects, unlocks flash + option bytes, writes OPTCR=0x0FFFAAEE, disconnects SWD.
 * After this call, STM32 is doing mass erase (~8-9s).
 * Caller MUST wait ~12s then ask user to press RESET, then call verify.
 * @return ESP_OK if OPTCR write succeeded and mass erase started.
 */
esp_err_t flasher_swd_rdp_disable_trigger(void);

/**
 * @brief PHASE 2: Verify RDP is now Level 0 after reset.
 * Reconnects via SWD, reads OPTCR, checks RDP=0xAA, reads flash to confirm.
 * Call this AFTER user pressed RESET on STM32.
 * @return ESP_OK if RDP Level 0 confirmed.
 */
esp_err_t flasher_swd_rdp_disable_verify(void);

#endif // __FLASHER_SWD_H__
