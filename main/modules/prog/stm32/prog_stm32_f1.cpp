/**
 * @file prog_stm32_f1.cpp
 * @brief Engine nạp firmware cho STM32F1 qua SWD.
 *
 * Kiến trúc giống flasher_swd_stm32f4.cpp:
 *   - Dual RAM strategy (full-malloc / 32KB streaming)
 *   - On-the-fly verify (256B chunks, 3 retries/chunk, 3 full attempts)
 *   - detect_rdp() warm-up trước flash session
 *
 * Khác biệt chính so với F4:
 *   - RDP detect: đọc FLASH_OBR trực tiếp (peripheral bus accessible dưới RDP1)
 *   - RDP disable: standard unlock flow (không cần blind write)
 *   - OB reload: SYSRESETREQ đủ (không cần rút điện)
 *   - Flash programming: half-word (library programBlock xử lý CSW toggle)
 *
 * Xem docs/stm32f1-swd-engine-spec.md cho chi tiết kỹ thuật.
 */

#include "prog_stm32_f1.h"

#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Adafruit_DAP.h"
#include "Arduino.h"
#include <stdio.h>
#include <sys/stat.h>
#include "../../storage/usb_drive/usb_drive.h"

static const char *TAG = "SWD_F1";

// ============================================================
// DAP INSTANCE & STATE
// ============================================================
static Adafruit_DAP_STM32F1 dap;
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
    dap.dap_write_word(STM32F1_DHCSR, 0xA05F0003);  // C_HALT + C_DEBUGEN
    dap.dap_write_word(STM32F1_DEMCR, 0x00000001);   // VC_CORERESET
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

static esp_err_t full_connect(uint32_t *idcode, uint32_t *chip_id) {
    #define SELECT_MAX_RETRIES 5

    for (int attempt = 1; attempt <= SELECT_MAX_RETRIES; attempt++) {
        esp_err_t ret = connect_target(idcode);
        if (ret != ESP_OK) return ret;

        // Clear DP sticky errors (STKERR, WDERR, ORUNERR)
        // Nguồn nhiễu có thể gây AHB-AP error → sticky bit chặn mọi transaction sau
        dap.dap_write_reg(0x00, 0x0000001E);

        halt_core();
        vTaskDelay(pdMS_TO_TICKS(50));

        *chip_id = 0;
        if (dap.select(chip_id)) {
            halt_core();
            uint32_t flash_kb = dap.target_device.flash_size / 1024;
            ESP_LOGI(TAG, "Chip: %s | ID: 0x%03" PRIx32 " | Flash: %" PRIu32 " KB | Page: %" PRIu32 "B",
                     dap.target_device.name ? dap.target_device.name : "unknown",
                     *chip_id, flash_kb, dap.getPageSize());
            return ESP_OK;
        }

        ESP_LOGW(TAG, "select() failed (ID: 0x%03" PRIx32 "), attempt %d/%d",
                 *chip_id, attempt, SELECT_MAX_RETRIES);

        // Full disconnect + delay → reconnect từ đầu (reset AHB-AP state)
        dap.dap_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGE(TAG, "Chip not recognized after %d attempts", SELECT_MAX_RETRIES);
    return ESP_FAIL;
}

static void full_disconnect(void) {
    dap.deselect();
    dap.dap_disconnect();
}

// ============================================================
// INIT / DEINIT
// ============================================================
esp_err_t flasher_swd_stm32f1_init(void) {
    if (g_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Init SWD (SWDIO=%d, SWCLK=%d)", SWD_SWDIO_PIN, SWD_SWCLK_PIN);

    if (!dap.begin(SWD_SWCLK_PIN, SWD_SWDIO_PIN, SWD_NRESET_PIN, dap_error_cb)) {
        ESP_LOGE(TAG, "DAP begin() failed");
        return ESP_FAIL;
    }

    g_initialized = true;
    return ESP_OK;
}

esp_err_t flasher_swd_stm32f1_deinit(void) {
    if (!g_initialized) return ESP_OK;

    dap.dap_disconnect();
    g_initialized = false;
    ESP_LOGI(TAG, "SWD deinitialized");
    return ESP_OK;
}

// ============================================================
// DETECT RDP
// ============================================================
/**
 * F1 advantage: FLASH_OBR (peripheral bus) is readable under RDP Level 1.
 * No guessing needed — bit RDPRT directly tells us the protection level.
 */
esp_err_t flasher_swd_stm32f1_detect_rdp(int *rdp_level) {
    ESP_LOGI(TAG, "======= DETECT RDP =======");

    esp_err_t ret = flasher_swd_stm32f1_init();
    if (ret != ESP_OK) return ret;

    uint32_t idcode = 0, chip_id = 0;
    ret = full_connect(&idcode, &chip_id);
    if (ret != ESP_OK) {
        flasher_swd_stm32f1_deinit();
        return ret;
    }

    // Read FLASH_OBR — accessible even under RDP1 (peripheral bus)
    uint32_t obr = dap.dap_read_word(STM32F1_FLASH_OBR);
    bool rdprt = (obr & STM32F1_OBR_RDPRT) != 0;

    ESP_LOGI(TAG, "FLASH_OBR=0x%08" PRIx32 " RDPRT=%d", obr, rdprt ? 1 : 0);

    if (rdp_level) {
        *rdp_level = rdprt ? 1 : 0;
    }

    // Cross-check: try reading flash at 0x08000000
    uint32_t flash_word = dap.dap_read_word(STM32F1_FLASH_START);
    ESP_LOGI(TAG, "Flash[0x08000000]=0x%08" PRIx32, flash_word);

    if (rdprt) {
        ESP_LOGW(TAG, ">>> RDP Level 1 — Flash protected");
    } else {
        ESP_LOGI(TAG, ">>> RDP Level 0 — Flash unprotected");
    }

    dap.dap_disconnect();
    flasher_swd_stm32f1_deinit();

    ESP_LOGI(TAG, "======= DETECT DONE =======");
    return ESP_OK;
}

// ============================================================
// RDP DISABLE — TRIGGER
// ============================================================
/**
 * Standard unlock flow (F1 peripheral reads work under RDP1):
 *   1. Unlock FPEC (KEY1+KEY2 → KEYR), verify LOCK=0
 *   2. Unlock Option Bytes (KEY1+KEY2 → OPTKEYR), verify OPTWRE=1
 *   3. Erase Option Bytes (OPTER + STRT), preserve OPTWRE
 *   4. Write 0xA5 to 0x1FFFF800 (CSW 16-bit, OPTPG mode), preserve OPTWRE
 *   5. Read back OB to verify 0xA5 was written
 *   6. SYSRESETREQ → OB reload → mass erase (3-8s), wait 10s
 *
 * CRITICAL: OPTWRE (bit 9) must be preserved throughout steps 3-4.
 * Writing CR=0 clears OPTWRE! Use OPTWRE bit in all CR writes.
 */
esp_err_t flasher_swd_stm32f1_rdp_disable_trigger(void) {
    ESP_LOGI(TAG, "======= RDP DISABLE: TRIGGER =======");

    esp_err_t ret = flasher_swd_stm32f1_init();
    if (ret != ESP_OK) return ret;

    uint32_t idcode = 0, chip_id = 0;
    ret = full_connect(&idcode, &chip_id);
    if (ret != ESP_OK) {
        flasher_swd_stm32f1_deinit();
        return ret;
    }

    // === Step 1: Unlock FPEC ===
    ESP_LOGI(TAG, "Step 1: Unlock FPEC...");
    dap.dap_write_word(STM32F1_FLASH_KEYR, STM32F1_FLASH_KEY1);
    dap.dap_write_word(STM32F1_FLASH_KEYR, STM32F1_FLASH_KEY2);

    uint32_t cr = dap.dap_read_word(STM32F1_FLASH_CR);
    if (cr & STM32F1_CR_LOCK) {
        ESP_LOGE(TAG, "FPEC unlock FAILED! CR=0x%08" PRIx32, cr);
        // Retry with SYSRESETREQ
        dap.dap_write_word(STM32F1_AIRCR, 0x05FA0004);
        delay(50);
        halt_core();
        dap.dap_write_word(STM32F1_FLASH_KEYR, STM32F1_FLASH_KEY1);
        dap.dap_write_word(STM32F1_FLASH_KEYR, STM32F1_FLASH_KEY2);
        cr = dap.dap_read_word(STM32F1_FLASH_CR);
        if (cr & STM32F1_CR_LOCK) {
            ESP_LOGE(TAG, "FPEC unlock FAILED after retry! CR=0x%08" PRIx32, cr);
            dap.dap_disconnect();
            flasher_swd_stm32f1_deinit();
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "  FPEC unlocked (CR=0x%08" PRIx32 ")", cr);

    // === Step 2: Unlock Option Bytes ===
    ESP_LOGI(TAG, "Step 2: Unlock Option Bytes...");
    dap.dap_write_word(STM32F1_FLASH_OPTKEYR, STM32F1_FLASH_KEY1);
    dap.dap_write_word(STM32F1_FLASH_OPTKEYR, STM32F1_FLASH_KEY2);

    cr = dap.dap_read_word(STM32F1_FLASH_CR);
    if (!(cr & STM32F1_CR_OPTWRE)) {
        ESP_LOGE(TAG, "Option unlock FAILED! OPTWRE=0, CR=0x%08" PRIx32, cr);
        dap.dap_disconnect();
        flasher_swd_stm32f1_deinit();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  Option Bytes unlocked (OPTWRE=1)");

    // === Step 3: Erase Option Bytes ===
    ESP_LOGI(TAG, "Step 3: Erase Option Bytes...");
    // OPTER must be set with OPTWRE preserved (read-modify-write style)
    dap.dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_OPTWRE | STM32F1_CR_OPTER);
    dap.dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_OPTWRE | STM32F1_CR_OPTER | STM32F1_CR_STRT);

    // Wait BSY (OB erase is fast, ~20ms)
    for (int t = 0; t < 100; t++) {
        uint32_t sr = dap.dap_read_word(STM32F1_FLASH_SR);
        if (!(sr & STM32F1_SR_BSY)) break;
        delay(10);
    }
    // Clear OPTER+STRT but PRESERVE OPTWRE — writing CR=0 clears OPTWRE!
    dap.dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_OPTWRE);

    // Verify OB erased — all bytes should be 0xFF
    uint32_t ob_val = dap.dap_read_word(STM32F1_OPT_RDP_ADDR);
    ESP_LOGI(TAG, "  OB erased, OPT_RDP=0x%08" PRIx32, ob_val);

    // === Step 4: Write RDP unlock code (0xA5) ===
    ESP_LOGI(TAG, "Step 4: Write RDP unlock (0xA5 → 0x1FFFF800)...");

    // Verify OPTWRE is still set before OPTPG
    cr = dap.dap_read_word(STM32F1_FLASH_CR);
    ESP_LOGI(TAG, "  CR before OPTPG: 0x%08" PRIx32 " (OPTWRE=%d)",
             cr, (cr & STM32F1_CR_OPTWRE) ? 1 : 0);

    if (!(cr & STM32F1_CR_OPTWRE)) {
        // OPTWRE lost — re-unlock OB
        ESP_LOGW(TAG, "  OPTWRE lost! Re-unlocking OB...");
        dap.dap_write_word(STM32F1_FLASH_OPTKEYR, STM32F1_FLASH_KEY1);
        dap.dap_write_word(STM32F1_FLASH_OPTKEYR, STM32F1_FLASH_KEY2);
        cr = dap.dap_read_word(STM32F1_FLASH_CR);
        if (!(cr & STM32F1_CR_OPTWRE)) {
            ESP_LOGE(TAG, "  OB re-unlock FAILED! CR=0x%08" PRIx32, cr);
            dap.dap_disconnect();
            flasher_swd_stm32f1_deinit();
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "  OB re-unlocked OK");
    }

    // Set OPTPG (preserve OPTWRE)
    dap.dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_OPTWRE | STM32F1_CR_OPTPG);

    // CSW → 16-bit for half-word write to Option Bytes
    uint32_t csw = dap.dap_read_reg(SWD_AP_CSW);
    dap.dap_write_reg(SWD_AP_CSW, (csw & ~0x07) | AP_CSW_SIZE_HALFWORD);

    // Replicate pattern for byte lane striping safety
    uint32_t safe = (uint32_t)STM32F1_RDP_UNLOCK_CODE |
                    ((uint32_t)STM32F1_RDP_UNLOCK_CODE << 16);
    dap.dap_write_word(STM32F1_OPT_RDP_ADDR, safe);

    // Wait BSY for OPTPG completion (fast, ~40μs)
    delay(5);
    for (int t = 0; t < 100; t++) {
        uint32_t sr = dap.dap_read_word(STM32F1_FLASH_SR);
        if (!(sr & STM32F1_SR_BSY)) break;
        delay(1);
    }

    // Restore CSW → 32-bit
    dap.dap_write_reg(SWD_AP_CSW, (csw & ~0x07) | AP_CSW_SIZE_WORD);

    // Clear OPTPG
    dap.dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_OPTWRE);

    // === Step 5: Verify OB write took effect ===
    ob_val = dap.dap_read_word(STM32F1_OPT_RDP_ADDR);
    ESP_LOGI(TAG, "Step 5: Readback OPT_RDP=0x%08" PRIx32 " (expect 0xA5 in low byte)", ob_val);

    if ((ob_val & 0xFF) != STM32F1_RDP_UNLOCK_CODE) {
        ESP_LOGE(TAG, "  OPTPG write FAILED! Got 0x%02" PRIx32 " expected 0xA5",
                 ob_val & 0xFF);
        // Dump diagnostic info
        uint32_t sr = dap.dap_read_word(STM32F1_FLASH_SR);
        cr = dap.dap_read_word(STM32F1_FLASH_CR);
        uint32_t obr = dap.dap_read_word(STM32F1_FLASH_OBR);
        ESP_LOGE(TAG, "  FLASH_SR=0x%08" PRIx32 " CR=0x%08" PRIx32 " OBR=0x%08" PRIx32,
                 sr, cr, obr);
        dap.dap_disconnect();
        flasher_swd_stm32f1_deinit();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  OB write verified OK!");

    // === Step 6: System Reset → OB reload triggers mass erase ===
    // On F1, changing RDP from Level 1→0 triggers mass erase during OB reload
    // Mass erase happens AFTER SYSRESETREQ, NOT before it
    ESP_LOGI(TAG, "Step 6: System Reset (SYSRESETREQ → OB reload + mass erase)...");
    dap.dap_write_word(STM32F1_AIRCR, 0x05FA0004);

    dap.dap_disconnect();
    flasher_swd_stm32f1_deinit();

    // Wait for mass erase to complete (3-8s depending on flash size)
    ESP_LOGI(TAG, "  Waiting 10s for mass erase...");
    vTaskDelay(pdMS_TO_TICKS(10000));

    ESP_LOGI(TAG, "======= RDP TRIGGER DONE =======");
    return ESP_OK;
}

// ============================================================
// RDP DISABLE — VERIFY
// ============================================================
static esp_err_t perform_rescue_erase(void) {
    ESP_LOGW(TAG, ">>> RESCUE MASS ERASE (Level 0 but flash not clean)");

    // Unlock
    dap.dap_write_word(STM32F1_FLASH_KEYR, STM32F1_FLASH_KEY1);
    dap.dap_write_word(STM32F1_FLASH_KEYR, STM32F1_FLASH_KEY2);

    uint32_t cr = dap.dap_read_word(STM32F1_FLASH_CR);
    if (cr & STM32F1_CR_LOCK) {
        ESP_LOGE(TAG, "Rescue: unlock failed! CR=0x%08" PRIx32, cr);
        return ESP_FAIL;
    }

    // Clear errors
    dap.dap_write_word(STM32F1_FLASH_SR, STM32F1_SR_ERR_CLEAR);

    // Mass Erase: MER + STRT
    dap.dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_MER);
    dap.dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_MER | STM32F1_CR_STRT);

    ESP_LOGI(TAG, "Rescue erase started, waiting BSY...");
    for (int t = 0; t < 150; t++) {
        uint32_t sr = dap.dap_read_word(STM32F1_FLASH_SR);
        if (!(sr & STM32F1_SR_BSY)) {
            if (sr & (STM32F1_SR_PGERR | STM32F1_SR_WRPRTERR)) {
                ESP_LOGE(TAG, "Rescue erase error! SR=0x%08" PRIx32, sr);
                return ESP_FAIL;
            }
            ESP_LOGI(TAG, ">>> RESCUE ERASE SUCCESS!");
            dap.dap_write_word(STM32F1_FLASH_CR, 0);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGE(TAG, "Rescue erase timeout!");
    dap.dap_write_word(STM32F1_FLASH_CR, 0);
    return ESP_FAIL;
}

esp_err_t flasher_swd_stm32f1_rdp_disable_verify(void) {
    ESP_LOGI(TAG, "======= RDP DISABLE: VERIFY =======");

    esp_err_t ret = flasher_swd_stm32f1_init();
    if (ret != ESP_OK) return ret;

    uint32_t idcode = 0;
    ret = connect_target(&idcode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reconnect FAILED");
        flasher_swd_stm32f1_deinit();
        return ESP_FAIL;
    }

    dap.dap_target_prepare();
    halt_core();

    // Read FLASH_OBR — check RDPRT
    uint32_t obr = dap.dap_read_word(STM32F1_FLASH_OBR);
    bool rdprt = (obr & STM32F1_OBR_RDPRT) != 0;
    ESP_LOGI(TAG, "FLASH_OBR=0x%08" PRIx32 " RDPRT=%d", obr, rdprt ? 1 : 0);

    if (rdprt) {
        ESP_LOGE(TAG, "RDP STILL ACTIVE!");
        dap.dap_disconnect();
        flasher_swd_stm32f1_deinit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, ">>> RDP Level 0 confirmed!");

    // Check flash is erased
    uint32_t flash_word = dap.dap_read_word(STM32F1_FLASH_START);
    ESP_LOGI(TAG, "Flash[0x08000000]=0x%08" PRIx32, flash_word);

    if (flash_word != 0xFFFFFFFF) {
        ret = perform_rescue_erase();
        if (ret == ESP_OK) {
            flash_word = dap.dap_read_word(STM32F1_FLASH_START);
            ESP_LOGI(TAG, "After rescue: Flash[0x08000000]=0x%08" PRIx32, flash_word);
        }
    } else {
        ESP_LOGI(TAG, "Flash is clean (0xFFFFFFFF)");
    }

    dap.dap_disconnect();
    flasher_swd_stm32f1_deinit();

    ESP_LOGI(TAG, "======= VERIFY DONE =======");
    return ESP_OK;
}

// ============================================================
// FLASH FIRMWARE
// ============================================================
esp_err_t flasher_swd_stm32f1_flash_firmware(const std::string& fw_path,
                                       flasher_swd_stm32f1_progress_cb_t on_progress) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  STM32F1 SWD FLASH");
    ESP_LOGI(TAG, "  File: %s", fw_path.c_str());
    ESP_LOGI(TAG, "========================================");

    #define SWD_SEGMENT_SIZE   (32 * 1024)
    #define SWD_PROGRAM_CHUNK  256
    #define SWD_MAX_ATTEMPTS   3
    #define SWD_CHUNK_RETRIES  3

    auto progress = [&](const char* text, int pct) {
        if (on_progress) on_progress(text, pct);
    };

    // ============================================================
    // STEP 1/5: OPEN FILE & MEMORY MANAGEMENT
    // ============================================================
    progress("Reading SD...", 1);
    ESP_LOGI(TAG, "[1/5] Opening firmware file...");

    std::string fw_full_path = std::string(USB_DRIVE_MOUNT) + fw_path;
    FILE* fwFile = fopen(fw_full_path.c_str(), "rb");
    if (!fwFile) {
        ESP_LOGE(TAG, "FAIL: Cannot open %s", fw_full_path.c_str());
        progress("File not found!", 0);
        return ESP_ERR_NOT_FOUND;
    }

    struct stat fw_st;
    stat(fw_full_path.c_str(), &fw_st);
    size_t file_size = (size_t)fw_st.st_size;
    if (file_size == 0) {
        ESP_LOGE(TAG, "FAIL: File is empty!");
        fclose(fwFile);
        progress("File empty!", 0);
        return ESP_FAIL;
    }

    // Pad to 4-byte alignment (verify uses 32-bit reads)
    size_t padded_size = (file_size + 3) & ~3;

    // Dual RAM strategy
    uint8_t* fw_buf = (uint8_t*)malloc(padded_size);
    bool streaming = false;
    size_t buf_size;

    if (fw_buf) {
        buf_size = padded_size;
        memset(fw_buf + file_size, 0xFF, padded_size - file_size);

        size_t total_read = 0;
        while (total_read < file_size) {
            size_t n = fread(fw_buf + total_read, 1, file_size - total_read, fwFile);
            if (n == 0) break;
            total_read += n;
        }
        fclose(fwFile);

        if (total_read != file_size) {
            ESP_LOGE(TAG, "FAIL: SD read incomplete: %zu / %zu", total_read, file_size);
            free(fw_buf);
            progress("SD read error!", 0);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "      Full-RAM: %zu bytes (%" PRIu32 " KB)",
                 file_size, (uint32_t)(file_size / 1024));
    } else {
        fw_buf = (uint8_t*)malloc(SWD_SEGMENT_SIZE);
        if (!fw_buf) {
            ESP_LOGE(TAG, "FAIL: Cannot allocate even %d bytes", SWD_SEGMENT_SIZE);
            fclose(fwFile);
            progress("Out of memory!", 0);
            return ESP_ERR_NO_MEM;
        }
        buf_size = SWD_SEGMENT_SIZE;
        streaming = true;
        ESP_LOGI(TAG, "      Streaming: %zu bytes FW, %d byte segments",
                 file_size, SWD_SEGMENT_SIZE);
    }

    // ============================================================
    // STEP 2/5: WARM-UP & RDP CHECK
    // ============================================================
    progress("Detecting...", 3);
    ESP_LOGI(TAG, "[2/5] Detecting chip + RDP (warm-up)...");

    int rdp_level = -1;
    esp_err_t ret = flasher_swd_stm32f1_detect_rdp(&rdp_level);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: Cannot connect to STM32F1");
        progress("Connect fail!", 0);
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ret;
    }

    if (rdp_level != 0) {
        ESP_LOGE(TAG, "FAIL: RDP Level %d — chip is protected!", rdp_level);
        progress("RDP locked!", 0);
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "      RDP Level 0 — OK");

    // ============================================================
    // STEP 3/5: CONNECT FOR FLASH
    // ============================================================
    progress("Connecting...", 5);
    ESP_LOGI(TAG, "[3/5] Connecting for flash...");

    ret = flasher_swd_stm32f1_init();
    if (ret != ESP_OK) {
        progress("SWD init fail!", 0);
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ret;
    }

    uint32_t idcode = 0, chip_id = 0;
    ret = full_connect(&idcode, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: Reconnect failed");
        progress("Connect fail!", 0);
        flasher_swd_stm32f1_deinit();
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ret;
    }

    // Validate flash size
    uint32_t flash_size = dap.target_device.flash_size;
    if (flash_size > 0) {
        ESP_LOGI(TAG, "      Chip: %s | Flash: %" PRIu32 " KB | FW: %" PRIu32 " KB",
                 dap.target_device.name ? dap.target_device.name : "unknown",
                 flash_size / 1024, (uint32_t)(file_size / 1024));

        if (file_size > flash_size) {
            ESP_LOGE(TAG, "FAIL: FW (%zu B) > flash (%" PRIu32 " B)!", file_size, flash_size);
            progress("FW too large!", 0);
            full_disconnect();
            flasher_swd_stm32f1_deinit();
            if (streaming) fclose(fwFile);
            free(fw_buf);
            return ESP_FAIL;
        }
    }

    // Verify buffer
    uint8_t* verify_buf = (uint8_t*)malloc(SWD_PROGRAM_CHUNK);
    if (!verify_buf) {
        ESP_LOGE(TAG, "FAIL: Cannot allocate verify buffer");
        full_disconnect();
        flasher_swd_stm32f1_deinit();
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ESP_ERR_NO_MEM;
    }

    // ============================================================
    // STEPS 4-5/5: ERASE → PROGRAM + VERIFY
    // ============================================================
    int total_chunks = (padded_size + SWD_PROGRAM_CHUNK - 1) / SWD_PROGRAM_CHUNK;
    bool flash_ok = false;

    for (int attempt = 1; attempt <= SWD_MAX_ATTEMPTS; attempt++) {
        if (attempt > 1) {
            ESP_LOGW(TAG, "=== RE-FLASH attempt %d/%d ===", attempt, SWD_MAX_ATTEMPTS);
            progress("Retry...", 3);

            full_disconnect();
            flasher_swd_stm32f1_deinit();
            delay(50);

            ret = flasher_swd_stm32f1_init();
            if (ret != ESP_OK) {
                free(verify_buf);
                if (streaming) fclose(fwFile);
                free(fw_buf);
                return ret;
            }

            ret = full_connect(&idcode, &chip_id);
            if (ret != ESP_OK) {
                flasher_swd_stm32f1_deinit();
                free(verify_buf);
                if (streaming) fclose(fwFile);
                free(fw_buf);
                return ret;
            }

            if (streaming) {
                fclose(fwFile);
                fwFile = fopen(fw_full_path.c_str(), "rb");
                if (!fwFile) {
                    ESP_LOGE(TAG, "FAIL: Cannot reopen file for retry");
                    full_disconnect();
                    flasher_swd_stm32f1_deinit();
                    free(verify_buf);
                    free(fw_buf);
                    return ESP_FAIL;
                }
            }
        }

        // --- STEP 4: PAGE ERASE ---
        progress("Erasing...", 8);
        ESP_LOGI(TAG, "[4/5] Erasing flash (pages)...");
        dap.program_start(STM32F1_FLASH_START, file_size);
        ESP_LOGI(TAG, "      Erase OK");

        // --- STEP 5: PROGRAM + VERIFY ---
        ESP_LOGI(TAG, "[5/5] Flashing %" PRIu32 " KB (%d chunks x %dB)...",
                 (uint32_t)(file_size / 1024), total_chunks, SWD_PROGRAM_CHUNK);

        uint32_t addr = STM32F1_FLASH_START;
        size_t file_offset = 0;
        int chunk_num = 0;
        int retry_count = 0;
        int last_pct = -1;
        bool this_attempt_ok = true;

        while (file_offset < padded_size) {
            size_t seg_size = padded_size - file_offset;
            if (seg_size > buf_size) seg_size = buf_size;

            uint8_t* seg_data;
            if (streaming) {
                size_t to_read = seg_size;
                if (file_offset + to_read > file_size) {
                    to_read = (file_offset < file_size) ? (file_size - file_offset) : 0;
                }
                size_t nread = 0;
                while (nread < to_read) {
                    size_t n = fread(fw_buf + nread, 1, to_read - nread, fwFile);
                    if (n == 0) break;
                    nread += n;
                }
                if (nread < to_read) {
                    ESP_LOGE(TAG, "SD read error at offset %zu", file_offset);
                    this_attempt_ok = false;
                    break;
                }
                if (seg_size > to_read) {
                    memset(fw_buf + to_read, 0xFF, seg_size - to_read);
                }
                seg_data = fw_buf;
            } else {
                seg_data = fw_buf + file_offset;
            }

            // Flash 256B chunks within segment
            size_t seg_offset = 0;
            while (seg_offset < seg_size) {
                size_t chunk = seg_size - seg_offset;
                if (chunk > SWD_PROGRAM_CHUNK) chunk = SWD_PROGRAM_CHUNK;
                chunk_num++;

                bool chunk_ok = false;
                for (int cr = 0; cr < SWD_CHUNK_RETRIES; cr++) {
                    dap.programBlock(addr, seg_data + seg_offset, chunk);
                    vTaskDelay(1);
                    bool read_ok = dap.dap_read_block(addr, verify_buf, chunk);

                    if (read_ok && memcmp(seg_data + seg_offset, verify_buf, chunk) == 0) {
                        chunk_ok = true;
                        break;
                    }

                    retry_count++;
                    if (read_ok) {
                        for (size_t i = 0; i < chunk; i += 4) {
                            uint32_t exp_w = *(const uint32_t*)(seg_data + seg_offset + i);
                            uint32_t got_w = *(const uint32_t*)(verify_buf + i);
                            if (exp_w != got_w) {
                                ESP_LOGW(TAG, "  [%d/%d] MISMATCH @0x%08" PRIx32
                                         " exp=0x%08" PRIx32 " got=0x%08" PRIx32
                                         " retry %d/%d",
                                         chunk_num, total_chunks, addr + (uint32_t)i,
                                         exp_w, got_w, cr + 1, SWD_CHUNK_RETRIES);
                                break;
                            }
                        }
                    } else {
                        ESP_LOGW(TAG, "  [%d/%d] READ FAIL @0x%08" PRIx32
                                 " retry %d/%d",
                                 chunk_num, total_chunks, addr, cr + 1, SWD_CHUNK_RETRIES);
                    }
                }

                if (!chunk_ok) {
                    ESP_LOGE(TAG, "  [%d/%d] FAILED after %d retries",
                             chunk_num, total_chunks, SWD_CHUNK_RETRIES);
                    this_attempt_ok = false;
                    break;
                }

                addr += chunk;
                seg_offset += chunk;

                // Progress: 8% → 95%
                size_t global_offset = file_offset + seg_offset;
                int pct = 8 + (int)((uint64_t)global_offset * 87 / padded_size);
                if (pct != last_pct) {
                    last_pct = pct;
                    char prog_text[32];
                    snprintf(prog_text, sizeof(prog_text), "Flash %d/%d",
                             chunk_num, total_chunks);
                    progress(prog_text, pct);
                }
            }

            if (!this_attempt_ok) break;
            file_offset += seg_size;
        }

        if (this_attempt_ok) {
            if (retry_count > 0) {
                ESP_LOGI(TAG, "      DONE! %d chunks OK (%d retries recovered)",
                         total_chunks, retry_count);
            } else {
                ESP_LOGI(TAG, "      DONE! %d chunks OK (no retries)", total_chunks);
            }
            flash_ok = true;
            break;
        }

        ESP_LOGW(TAG, "      Attempt %d/%d FAILED", attempt, SWD_MAX_ATTEMPTS);
    }

    free(verify_buf);
    if (streaming) fclose(fwFile);

    if (!flash_ok) {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "  FLASH FAILED after %d attempts!", SWD_MAX_ATTEMPTS);
        ESP_LOGE(TAG, "========================================");
        progress("Flash FAILED!", 0);
        full_disconnect();
        flasher_swd_stm32f1_deinit();
        free(fw_buf);
        return ESP_FAIL;
    }

    progress("Done!", 100);
    full_disconnect();
    flasher_swd_stm32f1_deinit();
    free(fw_buf);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  FLASH OK! %" PRIu32 " KB written", (uint32_t)(file_size / 1024));
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}
