/*
 * Tên Dự Án: ESP32 Host Flasher
 * Mô tả: Firmware quản lý nạp code đa năng (Offline SD + Online Git Sync)
 * Tác giả: TTP27
 * Ngày: 2025 (Refactored)
 */

 /* NOTE:
  * Đây là file main.cpp chính của dự án.
  * Mọi logic chính đều được điều phối từ đây.
  * Các module chức năng riêng biệt được tách ra trong thư mục /main
  * Tên File trên thẻ SD tối đa 8 ký tự, đuôi file tối đa 3 ký tự.
 */

#include "Arduino.h"
#include <Wire.h>

// ============================================================
// 1. ESP-IDF & SYSTEM HEADERS
// ============================================================
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================
// 2. PROJECT MODULES (CÁC MIẾNG GHÉP)
// ============================================================
#include "firmware_types.h"      // Struct dữ liệu chung
#include "sd_card/sd_card.h"            // Quản lý thẻ nhớ
#include "flasher/flasher.h"            // Lõi nạp (UART/Serial)
#include "oled/menu.h"                  // Giao diện OLED
#include "wifi_config/wifi_config.h"    // Quản lý Wifi
#include "ota_downloader/ota_downloader.h" // Tải file (Downloader)
#include "metadata_parser/metadata_parser.h"       // Đọc JSON (Parser)
#include "sync_engine/sync_engine.h"   // Đồng bộ Firmware (Sync Engine)

// ============================================================
// 3. CONSTANTS & GLOBALS
// ============================================================
static const char *TAG = "MAIN_APP";
bool force_delete = false;

#define SD_CS_PIN   GPIO_NUM_7
#define SDA_PIN     GPIO_NUM_8
#define SCL_PIN     GPIO_NUM_9
#define BUF_LEN     128

// Đối tượng màn hình
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Buffer tạm
static uint8_t buf[BUF_LEN] = {0};

// ============================================================
// 4. INTERNAL HELPER FUNCTIONS (HÀM XỬ LÝ RIÊNG BIỆT)
// ============================================================

/**
 * @brief Chế độ Monitor: Xem log từ Target qua UART
 */
static void run_monitor_mode() {
    ESP_LOGI(TAG, ">>> START MONITOR MODE");
    oled_show_message("Starting Monitor", "Check Logs...");
    vTaskDelay(pdMS_TO_TICKS(500));

    flasher_init();
    uart_flush_input(UART_NUM_1);
    uart_set_baudrate(UART_NUM_1, 115200);

    oled_show_message("Monitor Mode", "Press OK to Exit");

    while (1) {
        // Kiểm tra nút thoát
        if (digitalRead(BTN_OK) == LOW) {
            ESP_LOGI(TAG, ">>> EXIT MONITOR");
            oled_show_message("Exiting...", "");
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }
        
        // Đọc UART từ Target và in ra Log
        int rxBytes = uart_read_bytes(UART_NUM_1, buf, BUF_LEN - 1, 20 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            buf[rxBytes] = 0; // Null terminate
            ESP_LOGI("TARGET", "%s", buf);
        }
        vTaskDelay(10); // Yield
    }
}

/**
 * @brief Chế độ Sync: Đồng bộ Firmware từ GitHub
 */
static void run_sync_process() {
    
    ESP_LOGI(TAG, ">>> START SYNC PROCESS");
    oled_show_message("Confirm Sync", "UP=Yes DN=Cfg");

    // 1. Vòng lặp chờ xác nhận (UP: Chạy, DOWN: Cấu hình, OK: Hủy)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
        
        if (digitalRead(BTN_OK) == LOW) {
            ESP_LOGI(TAG, "Cancel Sync");
            return; // Thoát ngay
        }
        
        // --- Nút DOWN: Vào chế độ Cấu hình Wifi/URL ---
        if (digitalRead(BTN_DOWN) == LOW) {
            ESP_LOGI(TAG, "Enter Setup Config");
            wifi_config_force_portal(); // Hàm này blocking, xong sẽ tự thoát
            return; // Config xong thì thoát ra menu chính để chọn lại
        }

        // --- Nút UP: Bắt đầu Đồng bộ ---
        if (digitalRead(BTN_UP) == LOW) {
            unsigned long start = millis();
            while(digitalRead(BTN_UP) == LOW) {
                if(millis() - start > 3000) {
                    ESP_LOGW(TAG, "Detect Long Press -> FORCE CLEAN");
                    oled_show_message("Warning!", "FORCE RESET?");
                    delay(1000);
                    force_delete = true;
                    break;
                }
            }
            break; // Thoát vòng lặp chờ để xuống dưới xử lý
        }
    }

    // 2. Bắt đầu quy trình Sync
    oled_show_message("Connecting...", "Please wait");
    
    if (wifi_config_connect()) {
        
        ESP_LOGI(TAG, "WiFi Connected. Starting Sync Engine...");
        sync_engine_run(force_delete);
        vTaskDelay(2000);
    } 
    
    // 3. Dọn dẹp
    wifi_config_stop();
}

/**
 * @brief Chế độ Xóa Chip Target
 */
static void run_chip_erase() {
    ESP_LOGW(TAG, ">>> ERASE TARGET CHIP");
    oled_show_message("Erasing Chip...", "PLEASE WAIT!");

    flasher_init();
    if (flasher_chip_erase() == ESP_OK) {
        oled_show_message("SUCCESS!", "Chip Erased.");
    } else {
        oled_show_message("Error", "Erase Failed!");
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
}

/**
 * @brief Chế độ Nạp Firmware bình thường
 */
static void run_flash_fw(const std::string& fw_id) {
    ESP_LOGI(TAG, ">>> FLASH FW: %s", fw_id.c_str());

    std::string msg = "Flash: " + fw_id;
    oled_show_message("Flashing...", msg.c_str());

    // Kiểm tra thẻ nhớ lần nữa
    if (sd_mount(SD_CS_PIN) != ESP_OK) {
        oled_show_message("Error", "SD Card Lost!");
        vTaskDelay(2000);
        return;
    }

    flasher_init();

    if (flasher_begin_session(fw_id) == ESP_OK) {
        oled_show_message("Done!", "Flash Complete");
    } else {
        // Nếu fail, flasher sẽ tự restart HOST để retry với profile tiếp theo
        oled_show_message("Retrying...", "Check Log");
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
}

// ============================================================
// 5. ARDUINO SETUP & LOOP
// ============================================================

void setup() {
    // 1. Init System
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "--- SYSTEM START ---");

    // 2. Init OLED
    Wire.begin(SDA_PIN, SCL_PIN);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        ESP_LOGE(TAG, "OLED Init Failed");
        for(;;);
    }
    oled_show_message("Booting...", "Init Hardware");
    
    // 3. Init SD Card
    if (sd_mount(SD_CS_PIN) != ESP_OK) {
        oled_show_message("Error", "No SD Card");
        for(;;); // Dừng nếu không có thẻ (Tool này sống nhờ thẻ)
    }

    // 4. Load Data
    sd_load_metadata();

    // 5. Init Menu UI
    int menuLen = 0;
    const char** dispItems = sd_get_menu_display_items(menuLen);
    const char** idItems = sd_get_menu_id_items();
    
    if (menuLen == 0) {
        oled_show_message("Error", "Menu Empty!");
        for(;;);
    }
    
    menu_init(display, dispItems, idItems, menuLen);
    ESP_LOGI(TAG, "Ready!");
}

void loop() {
    // [1] Lắng nghe Menu
    int selectedIndex = menu_update();

    // [2] Nếu có chọn (Index >= 0) -> XỬ LÝ
    if (selectedIndex >= 0) {
        const char* fw_id_char = menu_get_id(selectedIndex);
        std::string fw_id(fw_id_char ? fw_id_char : "");

        ESP_LOGI(TAG, "Selected: %s", fw_id.c_str());

        if (fw_id.empty()) return;

        // --- BỘ ĐIỀU PHỐI (DISPATCHER) ---
        
        if (fw_id == "Monitor") {
            run_monitor_mode();
        }
        else if (fw_id == "SyncFW" || fw_id == "SYNC_CMD") {
            vTaskDelay(pdMS_TO_TICKS(300));
            run_sync_process();
        }
        else if (fw_id == "NULL" || fw_id == "Erase" || fw_id == "ERASE_CMD") {
            run_chip_erase();
        }
        else {
            // Mặc định: Nếu không phải lệnh hệ thống -> Là lệnh nạp FW
            run_flash_fw(fw_id);
        }

        // [3] Sau khi xử lý xong bất kỳ tác vụ nào -> Reset lại Menu/Hệ thống
        // Để đảm bảo RAM sạch sẽ và Menu cập nhật mới nhất
        host_system_restart();
    }

    vTaskDelay(pdMS_TO_TICKS(20)); // Delay nhẹ cho CPU thở
}

// ============================================================
// 6. ESP-IDF ENTRY POINT
// ============================================================
extern "C" void app_main() {
    initArduino();
    setup();
    while (true) {
        loop();
    }
}