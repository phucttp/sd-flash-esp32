/**
 * @file app_actions.cpp
 * @brief Các hành động cấp ứng dụng — giao diện giữa UI và các engine nạp/đồng bộ.
 *
 * Chức năng chính:
 *   - Điều phối quá trình nạp firmware cho cả ESP32 (UART) và STM32 (SWD)
 *   - Thực hiện xóa chip ESP32 (chip erase) độc lập với quá trình nạp
 *   - Chạy đồng bộ firmware từ git repository về SD card (sync engine)
 *   - Theo dõi và hiển thị log UART từ thiết bị target (monitor mode)
 *   - Bật/tắt chế độ NetFlash (nạp firmware từ xa qua mạng WiFi)
 *   - Quản lý trạng thái bận (busy), tiến trình và chuỗi trạng thái hiển thị trên UI
 */

#include "app_actions.h"
#include "Arduino.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../sd_card/sd_card.h"
#include "../flasher/flasher_common.h"
#include "../flasher/flasher_esp.h"
#include "../flasher/flasher_swd_stm32f4.h"
#include "../flasher/flasher_swd_stm32f1.h"
#include "../firmware_types.h"
#include "../usb_drive/usb_drive.h"
#include "../flash_log/flash_log.h"

#include <string>
#include <inttypes.h>

static const char* TAG = "APP_ACTIONS";

// ============================================================
// INTERNAL STATE
// ============================================================
static app_actions_config_t s_config = {0};
static volatile int s_progress = 0;
static volatile const char* s_status_text = "Ready";
static volatile bool s_is_busy = false;

// ============================================================
// MONITOR MODE - Internal
// ============================================================
#define LOG_LINES APP_ACTIONS_LOG_LINES
#define LOG_LINE_WIDTH APP_ACTIONS_LOG_LINE_WIDTH
#define MONITOR_BUF_LEN 128

static char s_log_buffer[LOG_LINES][LOG_LINE_WIDTH + 1];
static int s_log_write_index = 0;
static char s_uart_line_buffer[256];
static int s_uart_line_pos = 0;
static uint8_t s_monitor_buf[MONITOR_BUF_LEN] = {0};

static void log_buffer_init() {
    for (int i = 0; i < LOG_LINES; i++) {
        s_log_buffer[i][0] = '\0';
    }
    s_log_write_index = 0;
    s_uart_line_pos = 0;
    s_uart_line_buffer[0] = '\0';
}

static void log_buffer_add_line(const char* line) {
    strncpy(s_log_buffer[s_log_write_index], line, LOG_LINE_WIDTH);
    s_log_buffer[s_log_write_index][LOG_LINE_WIDTH] = '\0';
    s_log_write_index = (s_log_write_index + 1) % LOG_LINES;
}

static void log_process_uart_data(const char* data, int len) {
    for (int i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n' || c == '\r') {
            if (s_uart_line_pos > 0) {
                s_uart_line_buffer[s_uart_line_pos] = '\0';
                log_buffer_add_line(s_uart_line_buffer);
                s_uart_line_pos = 0;
            }
        } else if (s_uart_line_pos < (int)sizeof(s_uart_line_buffer) - 1) {
            s_uart_line_buffer[s_uart_line_pos++] = c;
        }
    }
}

// ============================================================
// HELPER MACROS
// ============================================================
#define SHOW_PROGRESS(text, pct) \
    do { if (s_config.on_progress) s_config.on_progress(text, pct); } while(0)

#define SHOW_MESSAGE(title, msg) \
    do { if (s_config.on_message) s_config.on_message(title, msg); } while(0)

#define SHOW_SPINNER(text, frame) \
    do { if (s_config.on_spinner) s_config.on_spinner(text, frame); } while(0)

#define ASK_CONFIRM(title, msg) \
    (s_config.on_confirm ? s_config.on_confirm(title, msg) : true)

// Check combo UP+DOWN de cancel action
static bool check_cancel_combo() {
    if (s_config.btn_up_pin < 0 || s_config.btn_down_pin < 0) return false;
    return (digitalRead(s_config.btn_up_pin) == LOW &&
            digitalRead(s_config.btn_down_pin) == LOW);
}

// Check cancel + hoi confirm
#define CHECK_CANCEL() \
    do { \
        if (check_cancel_combo()) { \
            if (ASK_CONFIRM("Cancel?", "Exit this action?")) { \
                ESP_LOGW(TAG, ">>> USER CANCELLED"); \
                s_is_busy = false; \
                s_status_text = "Ready"; \
                return ESP_ERR_INVALID_STATE; \
            } \
        } \
    } while(0)

// ============================================================
// INITIALIZATION
// ============================================================
void app_actions_init(const app_actions_config_t* config) {
    if (config) {
        s_config = *config;
    }
    s_progress = 0;
    s_status_text = "Ready";
    s_is_busy = false;
    ESP_LOGI(TAG, "App actions initialized");
}

// ============================================================
// STATUS API
// ============================================================
void app_actions_get_status(const char** text, int* progress, bool* is_busy) {
    if (text) *text = (const char*)s_status_text;
    if (progress) *progress = s_progress;
    if (is_busy) *is_busy = s_is_busy;
}

// ============================================================
// ACTION: MONITOR MODE
// ============================================================
esp_err_t action_monitor(void) {
    ESP_LOGI(TAG, ">>> START MONITOR MODE");

    SHOW_MESSAGE("Starting Monitor", "Waiting for data...");
    vTaskDelay(pdMS_TO_TICKS(500));

    s_is_busy = true;
    s_status_text = "Monitoring...";

    flasher_init();
    uart_flush_input(UART_NUM_1);
    uart_set_baudrate(UART_NUM_1, 115200);

    log_buffer_init();
    log_buffer_add_line("Waiting for Target...");
    log_buffer_add_line("Baudrate: 115200");
    log_buffer_add_line("--------------------");

    bool has_activity = false;
    unsigned long last_activity_time = 0;
    unsigned long last_display_update = 0;

    // Ve man hinh ban dau
    if (s_config.on_monitor_draw) {
        s_config.on_monitor_draw(s_log_buffer, s_log_write_index, false);
    }

    while (1) {
        // Kiem tra nut OK hoac combo UP+DOWN de thoat
        bool btn_ok = (s_config.btn_ok_pin >= 0 && digitalRead(s_config.btn_ok_pin) == LOW);
        bool combo_exit = check_cancel_combo();
        if (btn_ok || combo_exit) {
            ESP_LOGI(TAG, ">>> EXIT MONITOR");
            vTaskDelay(pdMS_TO_TICKS(1500));
            break;
        }

        // Doc du lieu UART
        int rx_bytes = uart_read_bytes(UART_NUM_1, s_monitor_buf,
                                        MONITOR_BUF_LEN - 1, 20 / portTICK_PERIOD_MS);
        if (rx_bytes > 0) {
            s_monitor_buf[rx_bytes] = 0;
            ESP_LOGI("TARGET", "%s", s_monitor_buf);
            log_process_uart_data((char*)s_monitor_buf, rx_bytes);
            has_activity = true;
            last_activity_time = millis();
        }

        // Cap nhat man hinh
        unsigned long now = millis();
        if (now - last_display_update >= 100) {
            last_display_update = now;
            if (now - last_activity_time > 1000) {
                has_activity = false;
            }
            if (s_config.on_monitor_draw) {
                s_config.on_monitor_draw(s_log_buffer, s_log_write_index, has_activity);
            }
        }

        vTaskDelay(10);
    }

    s_is_busy = false;
    s_status_text = "Ready";
    return ESP_OK;
}

// ============================================================
// ACTION: ERASE CHIP
// ============================================================
esp_err_t action_erase_chip(void) {
    ESP_LOGW(TAG, ">>> ERASE TARGET CHIP");

    if (!ASK_CONFIRM("Erase Chip?", "All data will be lost!")) {
        return ESP_ERR_INVALID_STATE;
    }

    s_is_busy = true;
    s_status_text = "Erasing...";
    s_progress = 0;

    SHOW_PROGRESS("Erasing chip...", 0);
    flasher_init();

    // Progress animation - co the cancel
    for (int i = 0; i <= 50; i += 10) {
        CHECK_CANCEL();
        s_progress = i;
        SHOW_PROGRESS("Erasing chip...", i);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_err_t ret = flasher_chip_erase();

    if (ret == ESP_OK) {
        s_progress = 100;
        SHOW_PROGRESS("Erasing chip...", 100);
        vTaskDelay(pdMS_TO_TICKS(500));
        SHOW_MESSAGE("SUCCESS!", "Chip Erased.");
        flash_log_write(LOG_ACTION_ERASE_ESP, NULL, LOG_RESULT_OK, NULL);
    } else {
        SHOW_MESSAGE("Error", "Erase Failed!");
        flash_log_write(LOG_ACTION_ERASE_ESP, NULL, LOG_RESULT_FAIL, NULL);
    }

    vTaskDelay(pdMS_TO_TICKS(2000));

    s_is_busy = false;
    s_status_text = "Ready";
    s_progress = 0;

    return ret;
}

// ============================================================
// ACTION: SCAN BOOT
// ============================================================
esp_err_t action_scan_boot(void) {
    ESP_LOGI(TAG, ">>> SCAN BOOT MODE");

    s_is_busy = true;
    s_status_text = "Scanning...";

    // Spinner animation - co the cancel
    for (int i = 0; i < 30; i++) {
        CHECK_CANCEL();
        SHOW_SPINNER("Scanning combos...", i);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    flasher_init();
    int combo_index = flasher_scan_and_save_combo();

    esp_err_t ret;
    if (combo_index >= 0) {
        char msg[24];
        snprintf(msg, sizeof(msg), "Saved Combo #%d", combo_index);
        SHOW_MESSAGE("SUCCESS!", msg);
        flash_log_write(LOG_ACTION_SCAN_BOOT, NULL, LOG_RESULT_OK, msg);
        ret = ESP_OK;
    } else {
        SHOW_MESSAGE("FAILED!", "No combo found");
        flash_log_write(LOG_ACTION_SCAN_BOOT, NULL, LOG_RESULT_FAIL, NULL);
        ret = ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(2000));

    s_is_busy = false;
    s_status_text = "Ready";

    return ret;
}

// ============================================================
// HELPER: RDP DISABLE WITH RETRY (max 3 attempts)
// ============================================================
#define SWD_RDP_MAX_RETRIES 3

static esp_err_t swd_rdp_disable_with_retry(bool is_f1) {
    for (int attempt = 1; attempt <= SWD_RDP_MAX_RETRIES; attempt++) {
        if (attempt > 1) {
            ESP_LOGW(TAG, "RDP disable retry %d/%d", attempt, SWD_RDP_MAX_RETRIES);
            SHOW_PROGRESS("Retry erase...", 20);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        SHOW_PROGRESS("RDP Disable...", 20);
        esp_err_t ret = is_f1 ? flasher_swd_stm32f1_rdp_disable_trigger()
                               : flasher_swd_stm32f4_rdp_disable_trigger();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "RDP trigger failed (attempt %d)", attempt);
            continue;
        }

        SHOW_PROGRESS("Erase done", 80);

        if (!is_f1) {
            // F4: manual power cycle required (no OBL_LAUNCH bit)
            SHOW_MESSAGE("POWER CYCLE", "STM32 now!");
            ESP_LOGW(TAG, ">>> Waiting for user to POWER CYCLE STM32...");
            if (s_config.btn_ok_pin >= 0) {
                while (digitalRead(s_config.btn_ok_pin) == LOW)
                    vTaskDelay(pdMS_TO_TICKS(50));
                while (digitalRead(s_config.btn_ok_pin) == HIGH)
                    vTaskDelay(pdMS_TO_TICKS(50));
                vTaskDelay(pdMS_TO_TICKS(100));
            } else {
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
        } else {
            // F1: SYSRESETREQ + 10s wait already done in trigger
            ESP_LOGI(TAG, "F1: OB reload done, verifying...");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        SHOW_PROGRESS("Verify RDP...", 85);
        ret = is_f1 ? flasher_swd_stm32f1_rdp_disable_verify()
                     : flasher_swd_stm32f4_rdp_disable_verify();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "RDP disable OK (attempt %d/%d)", attempt, SWD_RDP_MAX_RETRIES);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "RDP verify failed (attempt %d/%d)", attempt, SWD_RDP_MAX_RETRIES);
    }

    ESP_LOGE(TAG, "RDP disable FAILED after %d attempts", SWD_RDP_MAX_RETRIES);
    return ESP_FAIL;
}

// ============================================================
// ACTION: FLASH FIRMWARE
// ============================================================
esp_err_t action_flash_firmware(const char* fw_id) {
    if (!fw_id) {
        ESP_LOGE(TAG, "fw_id is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, ">>> FLASH FW: %s", fw_id);

    char msg[32];
    snprintf(msg, sizeof(msg), "Flash %s?", fw_id);

    if (!ASK_CONFIRM("Confirm", msg)) {
        return ESP_ERR_INVALID_STATE;
    }

    s_is_busy = true;
    s_status_text = "Connecting...";
    s_progress = 0;

    // Lock MSC — ngăn PC ghi đè file đang dùng
    usb_drive_lock();

    // Lay metadata de xac dinh device_type
    std::string fw_id_str(fw_id);
    firmware_metadata_t metadata;
    esp_err_t ret = sd_get_firmware_path(fw_id_str, metadata);
    if (ret != ESP_OK) {
        SHOW_MESSAGE("Error", "FW not found!");
        usb_drive_unlock();
        s_is_busy = false;
        vTaskDelay(pdMS_TO_TICKS(2000));
        return ret;
    }

    // Auto-detect target: STM32 → SWD, else → ESP UART
    bool is_stm32 = (metadata.device_type.find("STM32") != std::string::npos);

    s_status_text = "Flashing...";

    if (is_stm32) {
        ESP_LOGI(TAG, ">>> STM32 detected, probing IDCODE...");
        SHOW_MESSAGE("SWD Flash", metadata.device_type.c_str());
        vTaskDelay(pdMS_TO_TICKS(500));

        uint32_t idcode = swd_probe_idcode();
        bool is_f1 = (idcode == SWD_IDCODE_CORTEX_M3);
        bool is_f4 = (idcode == SWD_IDCODE_CORTEX_M4);

        if (!is_f1 && !is_f4) {
            ESP_LOGE(TAG, "Unknown IDCODE=0x%08" PRIx32, idcode);
            SHOW_MESSAGE("Error", "Unknown STM32!");
            usb_drive_unlock();
            s_is_busy = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Target: %s (IDCODE=0x%08" PRIx32 ")", is_f1 ? "F1" : "F4", idcode);

        // Detect RDP before flashing
        SHOW_PROGRESS("Detect RDP...", 3);
        int rdp_level = -1;
        esp_err_t rdp_ret = is_f1 ? flasher_swd_stm32f1_detect_rdp(&rdp_level)
                                   : flasher_swd_stm32f4_detect_rdp(&rdp_level);
        if (rdp_ret != ESP_OK) {
            SHOW_MESSAGE("Error", "SWD Connect Fail!");
            usb_drive_unlock();
            s_is_busy = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            return rdp_ret;
        }

        if (rdp_level == 1) {
            // RDP locked — ask user to erase + flash in one go
            if (!ASK_CONFIRM("RDP Locked!", "Erase + Flash?")) {
                usb_drive_unlock();
                s_is_busy = false;
                return ESP_ERR_INVALID_STATE;
            }

            esp_err_t erase_ret = swd_rdp_disable_with_retry(is_f1);
            if (erase_ret != ESP_OK) {
                SHOW_MESSAGE("Error", "Erase Failed!");
                usb_drive_unlock();
                s_is_busy = false;
                vTaskDelay(pdMS_TO_TICKS(2000));
                return erase_ret;
            }
            SHOW_MESSAGE("Erased!", "Now flashing...");
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (rdp_level == 2) {
            SHOW_MESSAGE("Error", "RDP L2 permanent!");
            usb_drive_unlock();
            s_is_busy = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            return ESP_FAIL;
        }

        // Flash (RDP Level 0 confirmed)
        auto swd_progress = [](const char* text, int pct) {
            if (s_config.on_progress) s_config.on_progress(text, pct);
        };
        if (is_f1) {
            ESP_LOGI(TAG, ">>> Cortex-M3 (F1), using STM32F1 engine");
            ret = flasher_swd_stm32f1_flash_firmware(metadata.path, swd_progress);
        } else {
            ESP_LOGI(TAG, ">>> Cortex-M4 (F4), using STM32F4 engine");
            ret = flasher_swd_stm32f4_flash_firmware(metadata.path, swd_progress);
        }
    } else {
        ESP_LOGI(TAG, ">>> ESP32 detected, using UART engine");
        flasher_init();

        // Progress connecting animation
        for (int i = 0; i <= 30; i += 5) {
            s_progress = i;
            SHOW_PROGRESS("Connecting...", i);
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        ret = flasher_begin_session(fw_id_str);
    }

    // Build detail string cho log
    const char* target = is_stm32 ? "SWD" : "ESP";
    char log_detail[48] = {0};
    if (ret == ESP_OK) {
        snprintf(log_detail, sizeof(log_detail), "%s|MD5:OK", target);
        s_progress = 100;
        s_status_text = "Done";
        SHOW_MESSAGE("Done!", "Flash Complete");
        flash_log_write(is_stm32 ? LOG_ACTION_FLASH_SWD : LOG_ACTION_FLASH_ESP,
                        fw_id, LOG_RESULT_OK, log_detail);
        vTaskDelay(pdMS_TO_TICKS(700));
    } else {
        snprintf(log_detail, sizeof(log_detail), "%s|ERR", target);
        s_status_text = "Error";
        SHOW_MESSAGE("Error", "Flash Failed!");
        flash_log_write(is_stm32 ? LOG_ACTION_FLASH_SWD : LOG_ACTION_FLASH_ESP,
                        fw_id, LOG_RESULT_FAIL, log_detail);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    usb_drive_unlock();
    s_is_busy = false;
    return ret;
}

// ============================================================
// ACTION: ERASE CHIP (SWD - STM32)
// ============================================================
esp_err_t action_erase_chip_swd(void) {
    ESP_LOGW(TAG, ">>> ERASE STM32 VIA SWD");

    if (!ASK_CONFIRM("SWD Erase?", "All data will be lost!")) {
        return ESP_ERR_INVALID_STATE;
    }

    s_is_busy = true;
    s_status_text = "Erasing (SWD)...";
    s_progress = 0;

    SHOW_PROGRESS("SWD Erase...", 0);

    // Step 0: Probe IDCODE to route F1 vs F4
    uint32_t idcode = swd_probe_idcode();
    bool is_f1 = (idcode == SWD_IDCODE_CORTEX_M3);
    bool is_f4 = (idcode == SWD_IDCODE_CORTEX_M4);

    if (!is_f1 && !is_f4) {
        ESP_LOGE(TAG, "Unknown IDCODE=0x%08" PRIx32, idcode);
        SHOW_MESSAGE("Error", "Unknown STM32!");
        flash_log_write(LOG_ACTION_ERASE_SWD, NULL, LOG_RESULT_FAIL, "UNKNOWN_IDCODE");
        s_is_busy = false;
        return ESP_FAIL;
    }

    const char* chip = is_f1 ? "F1" : "F4";
    ESP_LOGI(TAG, "Target: %s (IDCODE=0x%08" PRIx32 ")", chip, idcode);

    // Step 1: Detect RDP
    SHOW_PROGRESS("Detect RDP...", 10);
    int rdp_level = -1;
    esp_err_t ret;
    if (is_f1) {
        ret = flasher_swd_stm32f1_detect_rdp(&rdp_level);
    } else {
        ret = flasher_swd_stm32f4_detect_rdp(&rdp_level);
    }
    if (ret != ESP_OK) {
        SHOW_MESSAGE("Error", "SWD Connect Fail!");
        flash_log_write(LOG_ACTION_ERASE_SWD, NULL, LOG_RESULT_FAIL, "SWD_CONNECT");
        s_is_busy = false;
        return ret;
    }

    if (rdp_level == 0) {
        SHOW_PROGRESS("Verify L0...", 85);
        ret = is_f1 ? flasher_swd_stm32f1_rdp_disable_verify()
                     : flasher_swd_stm32f4_rdp_disable_verify();
        if (ret == ESP_OK) {
            s_progress = 100;
            SHOW_PROGRESS("Done!", 100);
            vTaskDelay(pdMS_TO_TICKS(500));
            SHOW_MESSAGE("SUCCESS!", "RDP L0 + Clean");
            flash_log_write(LOG_ACTION_ERASE_SWD, NULL, LOG_RESULT_OK, chip);
        } else {
            SHOW_MESSAGE("Error", "Rescue erase fail!");
            flash_log_write(LOG_ACTION_ERASE_SWD, NULL, LOG_RESULT_FAIL, "RESCUE_ERASE");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        s_is_busy = false;
        s_status_text = "Ready";
        s_progress = 0;
        return ret;
    }
    if (rdp_level == 2) {
        SHOW_MESSAGE("Error", "RDP L2 permanent!");
        flash_log_write(LOG_ACTION_ERASE_SWD, NULL, LOG_RESULT_FAIL, "RDP_L2");
        s_is_busy = false;
        return ESP_FAIL;
    }

    // Step 2: RDP disable with retry (max 3 attempts)
    ret = swd_rdp_disable_with_retry(is_f1);

    if (ret == ESP_OK) {
        s_progress = 100;
        SHOW_PROGRESS("Done!", 100);
        vTaskDelay(pdMS_TO_TICKS(500));
        SHOW_MESSAGE("SUCCESS!", "RDP L0 + Erased");
        flash_log_write(LOG_ACTION_ERASE_SWD, NULL, LOG_RESULT_OK, chip);
    } else {
        SHOW_MESSAGE("Error", "RDP still active!");
        flash_log_write(LOG_ACTION_ERASE_SWD, NULL, LOG_RESULT_FAIL, "RDP_ACTIVE");
    }

    vTaskDelay(pdMS_TO_TICKS(2000));

    s_is_busy = false;
    s_status_text = "Ready";
    s_progress = 0;

    return ret;
}
