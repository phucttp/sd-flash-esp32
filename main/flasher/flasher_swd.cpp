/**
 * @file flasher_swd.cpp
 * @brief STM32 SWD flasher engine — RDP detect & disable.
 *
 * KEY INSIGHT: Under RDP Level 1, ALL AHB-AP READS return 0x00000000.
 * This includes flash (0x08000000), SRAM (0x20000000), AND peripheral
 * registers (0x40023Cxx). Only PPB/CoreSight (0xE00xxxxx) is accessible.
 *
 * However, AHB-AP WRITES still go through to the bus — the flash controller
 * accepts unlock keys and OPTCR writes from the debug interface.
 *
 * APPROACH: Write flash unlock keys + option byte unlock keys + OPTCR
 * blindly (cannot verify via reads). Wait 15s for mass erase. Power cycle.
 */

#include "flasher_swd.h"

#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Adafruit_DAP.h"
#include "Arduino.h"

static const char *TAG = "SWD";

// ============================================================
// STM32 FLASH REGISTERS
// ============================================================
#define STM32_FLASH_KEYR    0x40023C04
#define STM32_FLASH_OPTKEYR 0x40023C08
#define STM32_FLASH_SR      0x40023C0C
#define STM32_FLASH_CR      0x40023C10
#define STM32_FLASH_OPTCR   0x40023C14

// Cortex-M debug registers (PPB — always accessible, even under RDP1)
#define DHCSR               0xE000EDF0
#define DCRSR               0xE000EDF4
#define DCRDR               0xE000EDF8
#define DEMCR               0xE000EDFC
#define AIRCR               0xE000ED0C

// ============================================================
// DAP INSTANCE & STATE
// ============================================================
static Adafruit_DAP_STM32 dap;
static bool g_initialized = false;

#define SWD_CONNECT_MAX_RETRIES  10
#define SWD_CONNECT_RETRY_MS     500

static void dap_error_cb(const char *msg) {
    ESP_LOGE(TAG, "DAP: %s", msg);
}

// ============================================================
// INTERNAL HELPERS
// ============================================================

static void halt_core(void) {
    dap.dap_write_word(DHCSR, 0xA05F0003);  // C_HALT + C_DEBUGEN
    dap.dap_write_word(DEMCR, 0x00000001);   // VC_CORERESET
    delay(15);
}

static esp_err_t connect_target(uint32_t *idcode) {
    for (int i = 1; i <= SWD_CONNECT_MAX_RETRIES; i++) {
        if (i > 1) {
            ESP_LOGW(TAG, "Retry %d/%d...", i, SWD_CONNECT_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(SWD_CONNECT_RETRY_MS));
        }

        if (!dap.targetConnect()) {
            ESP_LOGW(TAG, "targetConnect() failed (attempt %d)", i);
            continue;
        }

        *idcode = 0;
        dap.dap_read_idcode(idcode);
        if (*idcode != 0 && *idcode != 0xFFFFFFFF) {
            ESP_LOGI(TAG, "Connected! IDCODE=0x%08" PRIx32, *idcode);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Bad IDCODE: 0x%08" PRIx32, *idcode);
        dap.dap_disconnect();
    }

    ESP_LOGE(TAG, "Connect failed after %d attempts", SWD_CONNECT_MAX_RETRIES);
    return ESP_FAIL;
}

/** Full connect + select + halt (for when we need chip info & AHB reads) */
static esp_err_t full_connect(uint32_t *idcode, uint32_t *chip_id) {
    esp_err_t ret = connect_target(idcode);
    if (ret != ESP_OK) return ret;

    halt_core();

    *chip_id = 0;
    if (!dap.select(chip_id)) {
        ESP_LOGW(TAG, "select() failed (ID: 0x%03" PRIx32 "), retrying...", *chip_id);
        halt_core();
        *chip_id = 0;
        if (!dap.select(chip_id)) {
            ESP_LOGE(TAG, "Chip not recognized (ID: 0x%03" PRIx32 ")", *chip_id);
            dap.dap_disconnect();
            return ESP_FAIL;
        }
    }
    halt_core();

    ESP_LOGI(TAG, "Chip: %s | ID: 0x%03" PRIx32 " | Flash: %" PRIu32 " KB",
             dap.target_device.name, *chip_id, dap.target_device.flash_size / 1024);
    return ESP_OK;
}

static void full_disconnect(void) {
    dap.deselect();
    dap.dap_disconnect();
}

// ============================================================
// INIT / DEINIT
// ============================================================

esp_err_t flasher_swd_init(void) {
    if (g_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Init: SWCLK=GPIO%d SWDIO=GPIO%d nRESET=%d",
             SWD_SWCLK_PIN, SWD_SWDIO_PIN, SWD_NRESET_PIN);

    if (!dap.begin(SWD_SWCLK_PIN, SWD_SWDIO_PIN, SWD_NRESET_PIN, dap_error_cb)) {
        ESP_LOGE(TAG, "DAP begin() FAILED");
        return ESP_FAIL;
    }

    g_initialized = true;
    return ESP_OK;
}

esp_err_t flasher_swd_deinit(void) {
    if (!g_initialized) return ESP_OK;
    dap.dap_disconnect();
    g_initialized = false;
    ESP_LOGI(TAG, "SWD released");
    return ESP_OK;
}

// ============================================================
// DETECT RDP
// ============================================================

esp_err_t flasher_swd_detect_rdp(int *rdp_level) {
    ESP_LOGI(TAG, "======= DETECT RDP =======");

    esp_err_t ret = flasher_swd_init();
    if (ret != ESP_OK) return ret;

    uint32_t idcode = 0, chip_id = 0;
    ret = full_connect(&idcode, &chip_id);
    if (ret != ESP_OK) {
        flasher_swd_deinit();
        return ret;
    }

    uint32_t optcr = dap.dap_read_word(STM32_FLASH_OPTCR);
    uint8_t rdp_byte = (optcr >> 8) & 0xFF;

    ESP_LOGI(TAG, "OPTCR=0x%08" PRIx32 " RDP=0x%02X", optcr, rdp_byte);

    int level;
    if (rdp_byte == 0xAA) {
        level = 0;
        ESP_LOGI(TAG, ">>> RDP Level 0 — NO protection");
    } else if (rdp_byte == 0xCC) {
        level = 2;
        ESP_LOGE(TAG, ">>> RDP Level 2 — PERMANENT!");
    } else {
        level = 1;
        ESP_LOGW(TAG, ">>> RDP Level 1 — flash blocked via debug");
        ESP_LOGW(TAG, "    (OPTCR=0x00000000 under RDP1 is normal)");
    }

    uint32_t flash_word = dap.dap_read_word(0x08000000);
    ESP_LOGI(TAG, "Flash[0x08000000]=0x%08" PRIx32 " %s",
             flash_word, (flash_word == 0) ? "(blocked?)" : "(readable)");

    if (rdp_level) *rdp_level = level;

    full_disconnect();
    flasher_swd_deinit();

    ESP_LOGI(TAG, "======= DETECT RDP DONE (Level %d) =======", level);
    return ESP_OK;
}

// ============================================================
// PHASE 1: TRIGGER RDP DISABLE (DIRECT BLIND WRITES)
//
// Under RDP1, AHB-AP reads return 0 but WRITES go through.
// Strategy:
//   1. Connect SWD, prepare AP, halt core
//   2. SYSRESETREQ → clean flash controller state (all locked)
//   3. Re-halt via VC_CORERESET (before firmware runs)
//   4. Blind-write: flash unlock keys (2 words)
//   5. Blind-write: option byte unlock keys (2 words)
//   6. Blind-write: OPTCR = 0x0FFFAAEE (RDP=0xAA + OPTSTRT)
//   7. NO AHB-AP READS (would return 0 and corrupt TAR)
//   8. Wait 15s with IDCODE keepalive (DP level, always works)
//   9. Disconnect → user power cycles → verify
// ============================================================

esp_err_t flasher_swd_rdp_disable_trigger(void) {
    ESP_LOGI(TAG, "======= RDP DISABLE: TRIGGER (BLIND WRITES) =======");

    esp_err_t ret = flasher_swd_init();
    if (ret != ESP_OK) return ret;

    uint32_t idcode = 0;
    ret = connect_target(&idcode);
    if (ret != ESP_OK) {
        flasher_swd_deinit();
        return ret;
    }

    // Prepare AP (CSW, DP power) — required for AHB writes
    dap.dap_target_prepare();

    // Halt core + set VC_CORERESET
    halt_core();
    ESP_LOGI(TAG, "Core halted, sending SYSRESETREQ...");

    // SYSRESETREQ → reset chip to get clean flash controller state
    // After reset, VC_CORERESET halts core before firmware executes
    dap.dap_write_word(AIRCR, 0x05FA0004);
    delay(50);  // Wait for reset to complete

    // Re-halt (VC_CORERESET should have caught it, but be sure)
    halt_core();
    ESP_LOGI(TAG, "Core re-halted after reset");

    // ============================================================
    // BLIND WRITES — NO reads between these!
    // Under RDP1, reads return 0 and TAR auto-increment can
    // corrupt subsequent writes. Only pure writes here.
    // ============================================================

    ESP_LOGI(TAG, "Writing flash unlock keys...");
    dap.dap_write_word(STM32_FLASH_KEYR, 0x45670123);   // Key 1
    dap.dap_write_word(STM32_FLASH_KEYR, 0xCDEF89AB);   // Key 2

    ESP_LOGI(TAG, "Writing option byte unlock keys...");
    dap.dap_write_word(STM32_FLASH_OPTKEYR, 0x08192A3B); // Opt key 1
    dap.dap_write_word(STM32_FLASH_OPTKEYR, 0x4C5D6E7F); // Opt key 2

    ESP_LOGI(TAG, "Writing OPTCR = 0x0FFFAAEE (RDP=0xAA + OPTSTRT)...");
    dap.dap_write_word(STM32_FLASH_OPTCR, 0x0FFFAAEE);

    // ============================================================
    // WAIT for mass erase (~8-9s) + NVM commit
    // Use IDCODE reads (DP level) as keepalive — NOT AHB reads
    // ============================================================

    ESP_LOGI(TAG, "Waiting 15s for mass erase... DO NOT touch STM32!");
    for (int i = 15; i > 0; i--) {
        uint32_t id = 0;
        dap.dap_read_idcode(&id);  // DP level — always works
        ESP_LOGI(TAG, "  %ds... (IDCODE=0x%08" PRIx32 ")", i, id);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    dap.dap_disconnect();
    flasher_swd_deinit();

    ESP_LOGI(TAG, "======= TRIGGER DONE =======");
    ESP_LOGW(TAG, ">>> POWER CYCLE (rut dien cam) STM32 now! <<<");
    return ESP_OK;
}

// ============================================================
// RESCUE ERASE: Manual mass erase when chip is at Level 0
// but flash still has data (auto-erase failed/incomplete).
// Only works at RDP Level 0 (flash controller fully accessible).
// ============================================================

static esp_err_t perform_rescue_erase(void) {
    ESP_LOGW(TAG, ">>> Flash has data after RDP disable — rescue erase!");

    // Unlock flash
    dap.dap_write_word(STM32_FLASH_KEYR, 0x45670123);
    dap.dap_write_word(STM32_FLASH_KEYR, 0xCDEF89AB);

    // Verify unlock (at Level 0, reads work)
    uint32_t cr = dap.dap_read_word(STM32_FLASH_CR);
    if (cr & (1UL << 31)) {
        ESP_LOGE(TAG, "Flash unlock FAILED! CR=0x%08" PRIx32, cr);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Flash unlocked (CR=0x%08" PRIx32 ")", cr);

    // Clear sticky errors
    dap.dap_write_word(STM32_FLASH_SR, 0xF3);

    // Mass erase: MER + PSIZE_WORD + STRT
    dap.dap_write_word(STM32_FLASH_CR, 0x00000204);  // MER + PSIZE=x32
    dap.dap_write_word(STM32_FLASH_CR, 0x00010204);  // MER + PSIZE=x32 + STRT

    ESP_LOGI(TAG, "Erase started, waiting for BSY...");

    // Poll BSY with timeout (20s)
    for (int t = 0; t < 200; t++) {
        uint32_t sr = dap.dap_read_word(STM32_FLASH_SR);

        if (sr & (1UL << 16)) {
            // BSY set — erase in progress
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // BSY clear — check for errors
        if (sr & 0xF0) {  // PGSERR|PGPERR|PGAERR|WRPERR
            ESP_LOGE(TAG, "Erase error! SR=0x%08" PRIx32, sr);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, ">>> RESCUE ERASE SUCCESS! (SR=0x%08" PRIx32 ")", sr);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Erase timeout (20s)!");
    return ESP_FAIL;
}

// ============================================================
// PHASE 2: VERIFY RDP DISABLED
//
// After power cycle, reconnect WITHOUT select() (avoid SYSRESETREQ
// stale reads). Read OPTCR directly for true NVM value.
// If RDP=0xAA but flash not erased, perform rescue erase.
// ============================================================

esp_err_t flasher_swd_rdp_disable_verify(void) {
    ESP_LOGI(TAG, "======= RDP DISABLE: VERIFY =======");

    esp_err_t ret = flasher_swd_init();
    if (ret != ESP_OK) return ret;

    uint32_t idcode = 0;
    ret = connect_target(&idcode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reconnect FAILED — did you power cycle?");
        flasher_swd_deinit();
        return ESP_FAIL;
    }

    dap.dap_target_prepare();
    halt_core();

    // --- Read OPTCR (true NVM value after power cycle) ---
    uint32_t optcr = dap.dap_read_word(STM32_FLASH_OPTCR);
    uint8_t rdp_byte = (optcr >> 8) & 0xFF;
    ESP_LOGI(TAG, "OPTCR=0x%08" PRIx32 " RDP=0x%02X", optcr, rdp_byte);

    if (rdp_byte != 0xAA) {
        ESP_LOGE(TAG, "RDP STILL ACTIVE (0x%02X)!", rdp_byte);
        if (optcr == 0x00000000) {
            ESP_LOGE(TAG, "OPTCR=0 → RDP1 still blocking reads.");
            ESP_LOGE(TAG, "Writes may not have reached flash controller.");
        } else {
            ESP_LOGE(TAG, "Try: power cycle STM32 completely, then verify again.");
        }
        dap.dap_disconnect();
        flasher_swd_deinit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, ">>> RDP Level 0 confirmed!");

    // --- Check flash content ---
    uint32_t flash_word = dap.dap_read_word(0x08000000);
    ESP_LOGI(TAG, "Flash[0x08000000]=0x%08" PRIx32, flash_word);

    if (flash_word != 0xFFFFFFFF) {
        // Flash has data — auto-erase didn't complete, do rescue erase
        ret = perform_rescue_erase();
        if (ret == ESP_OK) {
            flash_word = dap.dap_read_word(0x08000000);
            ESP_LOGI(TAG, "After rescue: Flash[0x08000000]=0x%08" PRIx32, flash_word);
            if (flash_word == 0xFFFFFFFF) {
                ESP_LOGI(TAG, ">>> RECOVERY SUCCESSFUL! Chip is clean.");
            } else {
                ESP_LOGE(TAG, "Flash still has data after rescue erase.");
            }
        } else {
            ESP_LOGE(TAG, "Rescue erase failed. Try power cycling again.");
        }
    } else {
        ESP_LOGI(TAG, "Flash is clean (0xFFFFFFFF). All good!");
    }

    dap.dap_disconnect();
    flasher_swd_deinit();

    ESP_LOGI(TAG, "======= VERIFY DONE =======");
    return ESP_OK;
}
