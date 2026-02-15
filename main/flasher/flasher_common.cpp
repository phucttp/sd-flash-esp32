/**
 * @file flasher_common.cpp
 * @brief Shared utilities for all flasher engines.
 */

#include "flasher_common.h"

#include <string>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_ui.h"

// Extern UI instance from main.cpp
extern OledUI ui;

static const char *TAG = "FLASHER_COMMON";

// Current active flasher mode
static flasher_mode_t g_current_mode = FLASH_MODE_NONE;

flasher_mode_t flasher_get_current_mode(void) {
    return g_current_mode;
}

void host_system_restart() {
    ESP_LOGI(TAG, "Dang khoi dong lai he thong...");

    // Hiệu ứng dấu chấm động: Restarting. -> Restarting.. -> Restarting...
    for (int i = 1; i <= 3; i++) {
        std::string dots(i, '.');
        ui.showMessage("Restarting", dots.c_str());
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    esp_restart();
}
