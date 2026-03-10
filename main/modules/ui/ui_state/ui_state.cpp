/**
 * @file ui_state.cpp
 * @brief Lưu và khôi phục trạng thái giao diện giữa các lần restart.
 *
 * Lưu vào: /usb/config/ui_state.txt (FAT nội bộ thay vì SD card)
 * Format: tab,item_id,timestamp\n
 */

#include "ui_state.h"
#include "../../storage/usb_drive/usb_drive.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <inttypes.h>

static const char* TAG = "UI_STATE";

#define UI_STATE_FILE_PATH  USB_DRIVE_MOUNT "/config/ui_state.txt"
#define UI_STATE_CONFIG_DIR USB_DRIVE_MOUNT "/config"

// ============================================================
// SAVE
// ============================================================
esp_err_t ui_state_save(int tab_index, const char* item_id) {
    if (!item_id) return ESP_ERR_INVALID_ARG;

    size_t id_len = strlen(item_id);
    if (id_len >= UI_STATE_ITEM_ID_MAX_LEN) {
        ESP_LOGE(TAG, "item_id too long: %zu", id_len);
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    // Đảm bảo /usb/config tồn tại
    struct stat st;
    if (stat(UI_STATE_CONFIG_DIR, &st) != 0) {
        mkdir(UI_STATE_CONFIG_DIR, 0755);
    }

    FILE* f = fopen(UI_STATE_FILE_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open state file for write");
        return ESP_FAIL;
    }

    fprintf(f, "%d,%s,%" PRIu32 "\n", tab_index, item_id, ts);
    fclose(f);

    ESP_LOGI(TAG, "State saved: tab=%d item=%s ts=%" PRIu32, tab_index, item_id, ts);
    return ESP_OK;
}

// ============================================================
// LOAD
// ============================================================
esp_err_t ui_state_load(int* tab_index, char* item_id, size_t item_id_len) {
    if (!tab_index || !item_id) return ESP_ERR_INVALID_ARG;
    if (item_id_len < UI_STATE_ITEM_ID_MAX_LEN) return ESP_ERR_INVALID_ARG;

    struct stat st;
    if (stat(UI_STATE_FILE_PATH, &st) != 0) {
        ESP_LOGD(TAG, "State file not found");
        return ESP_ERR_NOT_FOUND;
    }

    FILE* f = fopen(UI_STATE_FILE_PATH, "r");
    if (!f) return ESP_FAIL;

    char line[128] = {0};
    bool got = (fgets(line, sizeof(line), f) != nullptr);
    fclose(f);

    if (!got || line[0] == '\0') {
        remove(UI_STATE_FILE_PATH);
        return ESP_ERR_INVALID_STATE;
    }

    int tab = -1;
    char id[UI_STATE_ITEM_ID_MAX_LEN] = {0};
    uint32_t saved_ts = 0;

    int parsed = sscanf(line, "%d,%31[^,],%" SCNu32, &tab, id, &saved_ts);
    if (parsed != 3 || tab < 0 || tab > 4) {
        ESP_LOGE(TAG, "State parse error: %s", line);
        remove(UI_STATE_FILE_PATH);
        return ESP_ERR_INVALID_STATE;
    }
    (void)saved_ts;

    *tab_index = tab;
    strncpy(item_id, id, item_id_len - 1);
    item_id[item_id_len - 1] = '\0';

    ESP_LOGI(TAG, "State loaded: tab=%d item=%s", tab, item_id);
    return ESP_OK;
}

// ============================================================
// CLEAR / EXISTS
// ============================================================
esp_err_t ui_state_clear(void) {
    if (remove(UI_STATE_FILE_PATH) == 0) {
        ESP_LOGI(TAG, "State cleared");
        return ESP_OK;
    }
    // File không tồn tại → cũng OK
    struct stat st;
    if (stat(UI_STATE_FILE_PATH, &st) != 0) return ESP_OK;
    ESP_LOGE(TAG, "Failed to clear state");
    return ESP_FAIL;
}

bool ui_state_exists(void) {
    struct stat st;
    return (stat(UI_STATE_FILE_PATH, &st) == 0 && S_ISREG(st.st_mode));
}
