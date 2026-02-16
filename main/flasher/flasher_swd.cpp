/**
 * @file flasher_swd.cpp
 * @brief STM32 SWD flasher engine — RDP detect/disable + flash programming.
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
#include "FS.h"
#include "SD.h"

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

    uint32_t flash_kb = dap.target_device.flash_size / 1024;
    if (flash_kb == 0) {
        ESP_LOGI(TAG, "Chip: %s | ID: 0x%03" PRIx32 " | Flash: unknown (RDP blocking reads?)",
                 dap.target_device.name, *chip_id);
    } else {
        ESP_LOGI(TAG, "Chip: %s | ID: 0x%03" PRIx32 " | Flash: %" PRIu32 " KB",
                 dap.target_device.name, *chip_id, flash_kb);
    }
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
    //
    // RELIABILITY FIX: Send the unlock+OPTCR sequence up to 3 times.
    // dap_write_word silently fails ~1/1000 words on ESP32-C3.
    // If ANY of the 5 critical writes fail, the sequence is broken.
    //
    // CRITICAL: Between retry attempts, we MUST issue SYSRESETREQ to
    // reset the flash controller's key state machine. The KEY registers
    // are sequential (KEY1 then KEY2). If KEY2 drops silently, the
    // controller is stuck in "waiting for KEY2" state. The KEY1 from
    // the next attempt would be interpreted as wrong KEY2 → LOCK.
    // SYSRESETREQ resets everything to a clean locked state.
    // ============================================================

    #define RDP_BLIND_WRITE_ATTEMPTS  3

    for (int bw = 1; bw <= RDP_BLIND_WRITE_ATTEMPTS; bw++) {
        if (bw > 1) {
            // Reset flash controller state machine via SYSRESETREQ
            ESP_LOGW(TAG, "Retry %d/%d: resetting chip first...", bw, RDP_BLIND_WRITE_ATTEMPTS);
            dap.dap_write_word(AIRCR, 0x05FA0004);  // SYSRESETREQ
            delay(50);
            halt_core();
        }

        ESP_LOGI(TAG, "Blind write attempt %d/%d...", bw, RDP_BLIND_WRITE_ATTEMPTS);

        // Flash unlock (KEY1 → KEY2 must be sequential, no drops!)
        dap.dap_write_word(STM32_FLASH_KEYR, 0x45670123);   // Key 1
        dap.dap_write_word(STM32_FLASH_KEYR, 0xCDEF89AB);   // Key 2

        // Option byte unlock (OPTKEY1 → OPTKEY2 must be sequential)
        dap.dap_write_word(STM32_FLASH_OPTKEYR, 0x08192A3B); // Opt key 1
        dap.dap_write_word(STM32_FLASH_OPTKEYR, 0x4C5D6E7F); // Opt key 2

        // Set RDP=0xAA + OPTSTRT — triggers mass erase + NVM commit
        dap.dap_write_word(STM32_FLASH_OPTCR, 0x0FFFAAEE);
    }

    ESP_LOGI(TAG, "Blind writes sent (%dx with reset between)", RDP_BLIND_WRITE_ATTEMPTS);

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

// ============================================================
// FLASH FIRMWARE via SWD
//
// Flow: connect → select → RDP check → erase sectors →
//       program (4KB chunks) → verify (4KB chunks) → deselect
// Requires RDP Level 0 (run Erase STM32 action first).
// Uses 0x08000000 (direct flash address) — NOT 0x00 alias
// because BOOT0 pin state affects the 0x00 memory remap.
// ============================================================

esp_err_t flasher_swd_flash_firmware(const std::string& fw_path,
                                      flasher_swd_progress_cb_t on_progress) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  STM32 SWD FLASH");
    ESP_LOGI(TAG, "  File: %s", fw_path.c_str());
    ESP_LOGI(TAG, "========================================");

    #define SWD_MAX_FW_SIZE  (128 * 1024)  // 128KB max — fits in ESP32-C3 RAM

    // Helper: report progress (safe if callback is NULL)
    auto progress = [&](const char* text, int pct) {
        if (on_progress) on_progress(text, pct);
    };

    // --- STEP 1: Read entire file into RAM ---
    // Buffer everything upfront so SD card SPI is completely idle during SWD ops.
    progress("Reading SD...", 1);
    ESP_LOGI(TAG, "[1/5] Reading file from SD...");

    File fwFile = SD.open(fw_path.c_str(), FILE_READ);
    if (!fwFile) {
        ESP_LOGE(TAG, "FAIL: Cannot open %s", fw_path.c_str());
        progress("File not found!", 0);
        return ESP_ERR_NOT_FOUND;
    }

    size_t file_size = fwFile.size();
    if (file_size == 0) {
        ESP_LOGE(TAG, "FAIL: File is empty!");
        fwFile.close();
        progress("File empty!", 0);
        return ESP_FAIL;
    }
    if (file_size > SWD_MAX_FW_SIZE) {
        ESP_LOGE(TAG, "FAIL: File too large: %zu bytes (max %d)", file_size, SWD_MAX_FW_SIZE);
        fwFile.close();
        progress("File too large!", 0);
        return ESP_FAIL;
    }

    // Pad to 4-byte alignment
    size_t padded_size = (file_size + 3) & ~3;
    uint8_t* fw_buf = (uint8_t*)malloc(padded_size);
    if (!fw_buf) {
        ESP_LOGE(TAG, "FAIL: Cannot allocate %zu bytes", padded_size);
        fwFile.close();
        progress("Out of memory!", 0);
        return ESP_ERR_NO_MEM;
    }

    // Fill padding with 0xFF (erased flash value)
    memset(fw_buf + file_size, 0xFF, padded_size - file_size);

    size_t total_read = 0;
    while (total_read < file_size) {
        size_t n = fwFile.read(fw_buf + total_read, file_size - total_read);
        if (n == 0) break;
        total_read += n;
    }
    fwFile.close();  // Done with SD card — no more SPI activity

    if (total_read != file_size) {
        ESP_LOGE(TAG, "FAIL: SD read incomplete: %zu / %zu bytes", total_read, file_size);
        free(fw_buf);
        progress("SD read error!", 0);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "      OK: %zu bytes (%" PRIu32 " KB) loaded into RAM",
             file_size, (uint32_t)(file_size / 1024));

    // --- STEP 2: Detect chip + RDP check (warm-up SWD) ---
    // detect_rdp() does a full self-contained cycle: init → connect → select → read → disconnect → deinit
    // This "warm-up" greatly improves the success rate of the next connection without needing reset.
    progress("Detecting...", 3);
    ESP_LOGI(TAG, "[2/5] Detecting chip + RDP (warm-up)...");

    int rdp_level = -1;
    esp_err_t ret = flasher_swd_detect_rdp(&rdp_level);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: Cannot connect to STM32");
        progress("Connect fail!", 0);
        free(fw_buf);
        return ret;
    }

    // Check RDP level
    if (rdp_level != 0) {
        ESP_LOGE(TAG, "FAIL: RDP Level %d — chip is protected!", rdp_level);
        ESP_LOGE(TAG, "      Run 'Erase STM32' to unlock first.");
        progress("RDP locked!", 0);
        free(fw_buf);
        return ESP_ERR_INVALID_STATE;  // Distinct from ESP_FAIL — means "need erase first"
    }
    ESP_LOGI(TAG, "      RDP Level 0 — OK");

    // --- STEP 3: Reconnect for flash operations ---
    progress("Connecting...", 5);
    ESP_LOGI(TAG, "[3/5] Connecting for flash...");

    ret = flasher_swd_init();
    if (ret != ESP_OK) {
        progress("SWD init fail!", 0);
        free(fw_buf);
        return ret;
    }

    uint32_t idcode = 0, chip_id = 0;
    ret = full_connect(&idcode, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: Reconnect failed");
        progress("Connect fail!", 0);
        flasher_swd_deinit();
        free(fw_buf);
        return ret;
    }

    // Validate flash size vs firmware size
    uint32_t flash_size = dap.target_device.flash_size;
    if (flash_size > 0) {
        ESP_LOGI(TAG, "      Chip: %s | Flash: %" PRIu32 " KB | FW: %" PRIu32 " KB",
                 dap.target_device.name ? dap.target_device.name : "unknown",
                 flash_size / 1024, (uint32_t)(file_size / 1024));
        if (file_size > flash_size) {
            ESP_LOGE(TAG, "FAIL: FW (%zu B) > flash (%" PRIu32 " B)!", file_size, flash_size);
            progress("FW too large!", 0);
            full_disconnect();
            flasher_swd_deinit();
            free(fw_buf);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "      Chip: %s | Flash size: unknown",
                 dap.target_device.name ? dap.target_device.name : "unknown");
    }

    // Allocate verify buffer for on-the-fly checking
    #define SWD_PROGRAM_CHUNK  256   // Small chunks = fresh unlock/PG per chunk
    #define SWD_MAX_ATTEMPTS   3
    #define SWD_CHUNK_RETRIES  3     // Per-chunk retry before full re-flash

    uint8_t* verify_buf = (uint8_t*)malloc(SWD_PROGRAM_CHUNK);
    if (!verify_buf) {
        ESP_LOGE(TAG, "FAIL: Cannot allocate verify buffer");
        full_disconnect();
        flasher_swd_deinit();
        free(fw_buf);
        return ESP_ERR_NO_MEM;
    }

    // --- STEP 4-5: Erase → Program+Verify on-the-fly → Retry ---
    // programBlock uses dap_write_word which can silently drop words (~1/1000).
    // Strategy: write 256B chunk → read back immediately → retry chunk on mismatch.
    // If chunk retry fails (bits stuck wrong), re-erase entire firmware and start over.

    int total_chunks = (padded_size + SWD_PROGRAM_CHUNK - 1) / SWD_PROGRAM_CHUNK;
    bool flash_ok = false;

    for (int attempt = 1; attempt <= SWD_MAX_ATTEMPTS; attempt++) {
        if (attempt > 1) {
            ESP_LOGW(TAG, "=== RE-FLASH attempt %d/%d ===", attempt, SWD_MAX_ATTEMPTS);
            progress("Retry...", 3);
            // Reconnect for fresh erase
            full_disconnect();
            flasher_swd_deinit();
            delay(50);
            ret = flasher_swd_init();
            if (ret != ESP_OK) { free(verify_buf); free(fw_buf); return ret; }
            ret = full_connect(&idcode, &chip_id);
            if (ret != ESP_OK) {
                flasher_swd_deinit();
                free(verify_buf); free(fw_buf);
                return ret;
            }
        }

        // --- Erase ---
        progress("Erasing...", 8);
        ESP_LOGI(TAG, "[4/5] Erasing flash...");
        dap.program_start(0x08000000, file_size);
        ESP_LOGI(TAG, "      Erase OK");

        // --- Program + Verify on-the-fly ---
        ESP_LOGI(TAG, "[5/5] Flashing %" PRIu32 " KB (%d chunks x %dB)...",
                 (uint32_t)(file_size / 1024), total_chunks, SWD_PROGRAM_CHUNK);

        uint32_t addr = 0x08000000;
        size_t offset = 0;
        int chunk_num = 0;
        int retry_count = 0;
        int last_pct = -1;
        bool this_attempt_ok = true;

        while (offset < padded_size) {
            size_t chunk = padded_size - offset;
            if (chunk > SWD_PROGRAM_CHUNK) chunk = SWD_PROGRAM_CHUNK;
            chunk_num++;

            bool chunk_ok = false;

            for (int cr = 0; cr < SWD_CHUNK_RETRIES; cr++) {
                // Write chunk
                dap.programBlock(addr, fw_buf + offset, chunk);

                // Small delay: let flash controller finish + WDT feed
                vTaskDelay(1);

                // Read back immediately (programBlock locks flash, so PG is cleared)
                bool read_ok = dap.dap_read_block(addr, verify_buf, chunk);

                if (read_ok && memcmp(fw_buf + offset, verify_buf, chunk) == 0) {
                    chunk_ok = true;
                    break;
                }

                // Mismatch — log first bad word and retry chunk
                retry_count++;
                if (read_ok) {
                    for (size_t i = 0; i < chunk; i += 4) {
                        uint32_t exp_w = *(const uint32_t*)(fw_buf + offset + i);
                        uint32_t got_w = *(const uint32_t*)(verify_buf + i);
                        if (exp_w != got_w) {
                            ESP_LOGW(TAG, "      [%d/%d] MISMATCH @0x%08" PRIx32
                                     " exp=0x%08" PRIx32 " got=0x%08" PRIx32 " retry %d/%d",
                                     chunk_num, total_chunks, addr + (uint32_t)i,
                                     exp_w, got_w, cr + 1, SWD_CHUNK_RETRIES);
                            break;
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "      [%d/%d] READ FAIL @0x%08" PRIx32 " retry %d/%d",
                             chunk_num, total_chunks, addr, cr + 1, SWD_CHUNK_RETRIES);
                }
            }

            if (!chunk_ok) {
                ESP_LOGE(TAG, "      [%d/%d] FAILED after %d retries — need full re-erase",
                         chunk_num, total_chunks, SWD_CHUNK_RETRIES);
                this_attempt_ok = false;
                break;
            }

            addr += chunk;
            offset += chunk;

            // Progress: 8% (erase) → 95% (flash done)
            int pct = 8 + (int)((uint64_t)offset * 87 / padded_size);
            if (pct != last_pct) {
                last_pct = pct;
                char prog_text[32];
                snprintf(prog_text, sizeof(prog_text), "Flash %d/%d", chunk_num, total_chunks);
                progress(prog_text, pct);
            }
        }

        if (this_attempt_ok) {
            if (retry_count > 0) {
                ESP_LOGI(TAG, "      DONE! %d/%d chunks OK (%d retries recovered)",
                         total_chunks, total_chunks, retry_count);
            } else {
                ESP_LOGI(TAG, "      DONE! %d/%d chunks OK (no retries needed)",
                         total_chunks, total_chunks);
            }
            flash_ok = true;
            break;
        }

        ESP_LOGW(TAG, "      Attempt %d/%d FAILED — will re-erase and retry",
                 attempt, SWD_MAX_ATTEMPTS);
    }

    free(verify_buf);

    if (!flash_ok) {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "  FLASH FAILED after %d attempts!", SWD_MAX_ATTEMPTS);
        ESP_LOGE(TAG, "  Check SWD wiring & target power");
        ESP_LOGE(TAG, "========================================");
        progress("Flash FAILED!", 0);
        full_disconnect();
        flasher_swd_deinit();
        free(fw_buf);
        return ESP_FAIL;
    }

    // --- Cleanup ---
    progress("Done!", 100);
    full_disconnect();
    flasher_swd_deinit();
    free(fw_buf);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  FLASH OK! %" PRIu32 " KB written", (uint32_t)(file_size / 1024));
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}
