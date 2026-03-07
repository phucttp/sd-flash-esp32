/**
 * @file flasher_common.cpp
 * @brief Các tiện ích dùng chung cho tất cả engine nạp firmware.
 *
 * Chức năng chính:
 *   - Theo dõi chế độ hoạt động hiện tại của flasher (ESP32 / STM32 / không có)
 *   - Hiển thị tiến trình và chuỗi trạng thái lên màn hình OLED trong quá trình nạp
 *   - Lưu trạng thái giao diện (tab + item đang chọn) và khởi động lại hệ thống
 */

#include "flasher_common.h"

#include <string>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tft_ui.h"
#include "../ui_state/ui_state.h"
#include "Adafruit_DAP.h"

// Extern UI instance from main.cpp
extern TftUI ui;

static const char *TAG = "FLASHER_COMMON";

// Current active flasher mode
static flasher_mode_t g_current_mode = FLASH_MODE_NONE;

flasher_mode_t flasher_get_current_mode(void) {
    return g_current_mode;
}

void host_system_restart(int tab, const char* item_id) {
    ESP_LOGI(TAG, "System restart with state save: tab=%d, item=%s", tab, item_id);

    // Lưu UI state trước khi restart
    esp_err_t save_ret = ui_state_save(tab, item_id);
    if (save_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save UI state! (error=%d) - restarting anyway", save_ret);
    } else {
        ESP_LOGI(TAG, "UI state saved successfully");
        // Small delay để đảm bảo SD card flush
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // // Show success animation (user không thấy "restart" → UX mượt mà hơn)
    // ui.showMessage("Complete!", "Success");

    // // Hold 2 giây để user thấy success message
    // vTaskDelay(pdMS_TO_TICKS(500));

    // Restart hệ thống
    esp_restart();
}

// ============================================================
// SWD IDCODE PROBE
// ============================================================
static void probe_error_cb(const char *msg) {
    ESP_LOGE("SWD_PROBE", "%s", msg);
}

uint32_t swd_probe_idcode(void) {
    ESP_LOGI(TAG, "Probing SWD IDCODE...");

    // Use Adafruit_DAP_STM32 as concrete class (only need DP-level IDCODE read)
    Adafruit_DAP_STM32 probe;
    if (!probe.begin(SWD_SWCLK_PIN, SWD_SWDIO_PIN, SWD_NRESET_PIN, probe_error_cb)) {
        ESP_LOGE(TAG, "Probe: begin() failed");
        return 0;
    }

    if (!probe.targetConnect()) {
        ESP_LOGE(TAG, "Probe: targetConnect() failed");
        return 0;
    }

    uint32_t idcode = 0;
    probe.dap_read_idcode(&idcode);
    probe.dap_disconnect();

    ESP_LOGI(TAG, "Probe IDCODE=0x%08" PRIx32, idcode);
    return idcode;
}
