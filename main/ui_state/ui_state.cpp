/**
 * @file ui_state.cpp
 * @brief Lưu và khôi phục trạng thái giao diện người dùng giữa các lần khởi động lại.
 *
 * Chức năng chính:
 *   - Ghi trạng thái hiện tại (tab đang mở, firmware đang chọn) vào file trên SD card
 *   - Đọc lại trạng thái đã lưu khi khởi động để tiếp tục từ vị trí cũ
 *   - Xóa trạng thái đã lưu sau khi khôi phục để tránh vòng lặp restart không mong muốn
 */

#include "ui_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "SD.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>  // For PRIu32

static const char* TAG = "ui_state";

// Đường dẫn file state trên SD card
#define UI_STATE_FILE_PATH "/config/ui_state.txt"

// ============================================================
// SAVE STATE
// ============================================================
esp_err_t ui_state_save(int tab_index, const char* item_id) {
    if (!item_id) {
        ESP_LOGE(TAG, "item_id is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Validate item_id length
    size_t id_len = strlen(item_id);
    if (id_len >= UI_STATE_ITEM_ID_MAX_LEN) {
        ESP_LOGE(TAG, "item_id too long: %zu chars (max %d)",
                 id_len, UI_STATE_ITEM_ID_MAX_LEN - 1);
        return ESP_ERR_INVALID_ARG;
    }

    // Get timestamp (seconds since boot)
    uint32_t timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    // Format: tab,item_id,timestamp
    char state_str[128];
    snprintf(state_str, sizeof(state_str), "%d,%s,%" PRIu32 "\n",
             tab_index, item_id, timestamp);

    // Tạo thư mục /config nếu chưa có
    if (!SD.exists("/config")) {
        if (!SD.mkdir("/config")) {
            ESP_LOGE(TAG, "Cannot create /config directory");
            return ESP_FAIL;
        }
    }

    // Ghi file (overwrite nếu tồn tại)
    File file = SD.open(UI_STATE_FILE_PATH, FILE_WRITE);
    if (!file) {
        ESP_LOGE(TAG, "Cannot open %s for writing", UI_STATE_FILE_PATH);
        return ESP_FAIL;
    }

    size_t written = file.print(state_str);
    file.close();

    if (written == 0) {
        ESP_LOGE(TAG, "Failed to write state file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "State saved: tab=%d, item=%s, ts=%" PRIu32,
             tab_index, item_id, timestamp);
    return ESP_OK;
}

// ============================================================
// LOAD STATE
// ============================================================
esp_err_t ui_state_load(int* tab_index, char* item_id, size_t item_id_len) {
    if (!tab_index || !item_id) {
        ESP_LOGE(TAG, "Output pointers are NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (item_id_len < UI_STATE_ITEM_ID_MAX_LEN) {
        ESP_LOGE(TAG, "item_id buffer too small: %zu (need %d)",
                 item_id_len, UI_STATE_ITEM_ID_MAX_LEN);
        return ESP_ERR_INVALID_ARG;
    }

    // Kiểm tra file tồn tại
    if (!SD.exists(UI_STATE_FILE_PATH)) {
        ESP_LOGD(TAG, "State file not found (normal on first boot)");
        return ESP_ERR_NOT_FOUND;
    }

    // Đọc file
    File file = SD.open(UI_STATE_FILE_PATH, FILE_READ);
    if (!file) {
        ESP_LOGE(TAG, "Cannot open %s for reading", UI_STATE_FILE_PATH);
        return ESP_FAIL;
    }

    // Đọc toàn bộ nội dung (tối đa 127 bytes)
    char line[128];
    size_t len = file.readBytesUntil('\n', line, sizeof(line) - 1);
    file.close();

    if (len == 0) {
        ESP_LOGE(TAG, "State file is empty");
        SD.remove(UI_STATE_FILE_PATH);  // Xóa file corrupt
        return ESP_ERR_INVALID_STATE;
    }
    line[len] = '\0';  // Null-terminate

    // Parse: tab,item_id,timestamp
    int tab = -1;
    char id[UI_STATE_ITEM_ID_MAX_LEN];
    uint32_t saved_ts = 0;

    int parsed = sscanf(line, "%d,%31[^,],%" SCNu32, &tab, id, &saved_ts);
    if (parsed != 3) {
        ESP_LOGE(TAG, "Invalid state format: %s (parsed %d fields)", line, parsed);
        SD.remove(UI_STATE_FILE_PATH);  // Xóa file corrupt
        return ESP_ERR_INVALID_STATE;
    }

    // Validate tab index
    if (tab < 0 || tab > 3) {
        ESP_LOGE(TAG, "Invalid tab index: %d", tab);
        SD.remove(UI_STATE_FILE_PATH);
        return ESP_ERR_INVALID_STATE;
    }

    // NOTE: Không validate timestamp vì esp_timer_get_time() reset về 0 mỗi boot
    // State file sẽ bị xóa ngay sau restore nên không cần timeout check
    (void)saved_ts;  // Suppress unused variable warning

    // State hợp lệ → return
    *tab_index = tab;
    strncpy(item_id, id, item_id_len - 1);
    item_id[item_id_len - 1] = '\0';

    ESP_LOGI(TAG, "State loaded: tab=%d, item=%s", tab, item_id);
    return ESP_OK;
}

// ============================================================
// CLEAR STATE
// ============================================================
esp_err_t ui_state_clear(void) {
    if (!SD.exists(UI_STATE_FILE_PATH)) {
        ESP_LOGD(TAG, "State file already deleted");
        return ESP_OK;
    }

    if (SD.remove(UI_STATE_FILE_PATH)) {
        ESP_LOGI(TAG, "State file cleared");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to delete state file");
        return ESP_FAIL;
    }
}

// ============================================================
// CHECK EXISTS
// ============================================================
bool ui_state_exists(void) {
    return SD.exists(UI_STATE_FILE_PATH);
}
