/*
 * Tên Dự Án: ESP32 Host Flasher
 * Mô tả: Firmware cho thiết bị Host ESP32, thực hiện chức năng nạp firmware
 * cho chip Target thông qua giao tiếp UART/SPI, sử dụng thẻ SD để lưu trữ
 * và màn hình OLED để hiển thị menu.
 * Tác giả: TTP27
 * Ngày: 2025
 */

#include "Arduino.h"
#include <Wire.h>

// ============================================================
// 1. ESP-IDF DRIVERS & LIBRARIES
// ============================================================
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================
// 2. PROJECT MODULES (THƯ VIỆN DỰ ÁN)
// ============================================================
#include "sd_card/sd_card.h"  // Quản lý file system trên thẻ SD
#include "flasher/flasher.h"  // Lõi xử lý nạp firmware (Flasher Core)
#include "oled/menu.h"        // Giao diện người dùng trên OLED

// ============================================================
// 3. DEFINITIONS & CONSTANTS (ĐỊNH NGHĨA & HẰNG SỐ)
// ============================================================
static const char *TAG = "MAIN_APP"; // Tag log chính cho ứng dụng

#define SD_CS_PIN   GPIO_NUM_7       // Chân Chip Select cho thẻ SD
#define BUF_LEN     128              // Độ dài buffer tạm (nếu dùng)

// ============================================================
// 4. GLOBAL OBJECTS & VARIABLES (BIẾN TOÀN CỤC)
// ============================================================
// Đối tượng màn hình OLED (SSD1306), sử dụng I2C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Buffer tạm (có thể dùng cho các tác vụ xử lý chuỗi nhỏ)
static uint8_t buf[BUF_LEN] = {0};

// Handle cho task giám sát (nếu triển khai sau này)
TaskHandle_t monitor_task_handle = NULL;

// ============================================================
// 5. MAIN FUNCTIONS IMPLEMENTATION
// ============================================================

/**
 * @brief  Khởi tạo hệ thống (System Setup)
 * @note   Hàm này chỉ chạy 1 lần khi khởi động.
 * Thực hiện khởi tạo các ngoại vi: Log, OLED, SD Card, và Menu.
 */
void setup() {
    // [1] Cấu hình Logging
    esp_log_level_set("*", ESP_LOG_INFO); // Đặt mức log mặc định là INFO
    ESP_LOGI(TAG, "========== SYSTEM BOOT START ==========");

    // [2] Khởi tạo màn hình OLED
    // SSD1306_SWITCHCAPVCC để tạo điện áp hiển thị từ 3.3V nội bộ
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        ESP_LOGE(TAG, "[CRITICAL] Khong tim thay OLED! Dung he thong.");
        for (;;); // Vòng lặp vô tận nếu lỗi phần cứng quan trọng
    }
    oled_show_message("Booting...", "System Init OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    // [3] Khởi tạo thẻ nhớ SD
    if (sd_mount(SD_CS_PIN) != ESP_OK) {
        ESP_LOGE(TAG, "[CRITICAL] Mount SD Card that bai!");
        oled_show_message("ERROR", "SD Mount Failed!");
        for (;;);
    }
    oled_show_message("Booting...", "SD Card OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    // [4] Tải cấu hình và Menu từ thẻ SD
    ESP_LOGI(TAG, "Dang tai metadata va xay dung menu...");
    sd_load_metadata(); // Đọc file cấu hình (vd: index.txt) để lấy danh sách FW

    // Lấy dữ liệu menu đã được module SD phân tích
    int menuLength = 0;
    const char** displayItems = sd_get_menu_display_items(menuLength);
    const char** idItems = sd_get_menu_id_items();

    // Kiểm tra tính hợp lệ của dữ liệu menu
    if (menuLength == 0 || displayItems == NULL || idItems == NULL) {
        ESP_LOGE(TAG, "[ERROR] Menu rong hoac loi du lieu! Kiem tra the SD.");
        oled_show_message("ERROR", "Menu Data Empty!");
        for(;;);
    }

    // [5] Khởi tạo giao diện Menu
    // Truyền dữ liệu đã lấy từ SD vào module hiển thị
    menu_init(display, displayItems, idItems, menuLength);
    
    ESP_LOGI(TAG, "========== SYSTEM BOOT COMPLETE ==========");
    ESP_LOGI(TAG, "Hien thi menu chinh.");
}

/**
 * @brief  Vòng lặp chính (Main Loop)
 * @note   Xử lý logic tương tác người dùng và điều phối quy trình nạp.
 */
void loop() {
    // [1] Cập nhật trạng thái Menu & Nút nhấn
    // Trả về -1 nếu chưa chọn, hoặc index >= 0 nếu đã nhấn OK
    int selectedIndex = menu_update();

    // [2] Xử lý khi người dùng chọn một mục (selectedIndex != -1)
    if (selectedIndex >= 0) {
        // Lấy Firmware ID tương ứng với mục đã chọn
        const char* fw_id_char = menu_get_id(selectedIndex);
        std::string fw_id_to_flash(fw_id_char ? fw_id_char : "");

        ESP_LOGI(TAG, "USER SELECTED: Index=%d, ID='%s'", selectedIndex, fw_id_to_flash.c_str());

        // [3] Phân loại hành động dựa trên ID
        if (!fw_id_to_flash.empty() && fw_id_to_flash != "NULL") {
            // === TRƯỜNG HỢP 1: NẠP FIRMWARE (ID hợp lệ) ===
            ESP_LOGI(TAG, ">>> Bat dau quy trinh FLASH FW: %s", fw_id_to_flash.c_str());
            
            std::string msg = "Flashing: " + fw_id_to_flash;
            oled_show_message("Please wait...", msg.c_str());

            // Lưu ý: Kiểm tra lại việc gọi sd_mount lần 2 ở đây có cần thiết không.
            // Nếu thẻ SD không bị tháo ra, có thể bỏ qua bước này để tối ưu.
            if (sd_mount(SD_CS_PIN) != ESP_OK) {
                 ESP_LOGE(TAG, "SD Card mat ket noi truoc khi flash!");
                 oled_show_message("ERROR", "SD Lost!");
                 for (;;);
            }

            // -- Bắt đầu nạp --
            flasher_init(); // Khởi tạo các chân/cấu hình cho flasher
            ESP_LOGI(TAG, "Flasher Core ready.");

            if(flasher_begin_session(fw_id_to_flash) != ESP_OK){
                ESP_LOGE(TAG, ">>> FLASH THAT BAI!");
                oled_show_message("Error", "Flash Failed!");
                vTaskDelay(pdMS_TO_TICKS(3000)); // Giữ thông báo lỗi lâu hơn chút
            } else {
                ESP_LOGI(TAG, ">>> FLASH THANH CONG!");
                oled_show_message("SUCCESS!", "Flash Complete.");
                vTaskDelay(pdMS_TO_TICKS(2000));
            }

        } else {
            // === TRƯỜNG HỢP 2: XÓA CHIP (ID là "NULL" hoặc Entry đặc biệt) ===
            // Thường dùng cho mục "Exit" hoặc "Erase Chip" trong menu
            ESP_LOGW(TAG, ">>> Phat hien yeu cau CHIP ERASE (ID=NULL)");
            oled_show_message("Erasing Chip...", "PLEASE WAIT!");
            
            flasher_init();
            if(flasher_chip_erase() != ESP_OK){
                ESP_LOGE(TAG, ">>> ERASE THAT BAI!");
                oled_show_message("Error", "Erase Failed!");
                vTaskDelay(pdMS_TO_TICKS(2000));
            } else {
                ESP_LOGI(TAG, ">>> ERASE THANH CONG!");
                oled_show_message("SUCCESS!", "Chip Erased.");
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        // [4] Khởi động lại Host sau khi hoàn tất tác vụ
        ESP_LOGI(TAG, "Yeu cau khoi dong lai Host...");
        vTaskDelay(pdMS_TO_TICKS(500)); // Đợi log đẩy hết ra UART
        host_system_restart(); 
    }

    // Delay nhỏ để tránh chiếm dụng 100% CPU (Watchdog triggering)
    vTaskDelay(pdMS_TO_TICKS(10)); 
}

// ============================================================
// 6. ESP-IDF APP MAIN ENTRY POINT
// ============================================================
/**
 * @brief  Điểm vào chính của ứng dụng ESP-IDF
 * @note   Cần thiết khi sử dụng cấu trúc dự án ESP-IDF nhưng viết code kiểu Arduino.
 */
extern "C" void app_main() {
    // Khởi tạo môi trường tương thích Arduino (CỰC KỲ QUAN TRỌNG)
    initArduino();
    
    // Gọi hàm setup() của Arduino
    setup();

    // Vòng lặp vĩnh cửu gọi loop()
    while (true) {
        loop();
    }
}