#include "Arduino.h"
#include "esp_log.h"
#include <Wire.h>

// --- MODULES CỦA BRO ---
#include "sd_card/sd_card.h"
#include "flasher/flasher.h"
#include "oled/menu.h" 
#include "driver/uart.h"

// -- Toàn cục ---
// (MỚI) Khai báo biến handle ở ngoài cùng (biến toàn cục)
TaskHandle_t monitor_task_handle = NULL;

// --- TAG ĐỂ LOGGING ---
static const char *TAG = "MAIN";

#define BUF_LEN 128

static uint8_t buf[BUF_LEN] = {0};

// --- CẤU HÌNH PHẦN CỨNG ---
// Các #define cho OLED (SCREEN_WIDTH, v.v.) và
// Nút nhấn (BTN_UP, v.v.) đã nằm trong "oled/menu.h"
#define SD_CS   GPIO_NUM_7 // Chip Select cho thẻ SD

// Khởi tạo đối tượng display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/**
 * @brief HÀM SETUP CHÍNH
 */
void setup() {
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "--- BAT DAU KHOI DONG HE THONG ---");

    // 1. Khởi động OLED (Không đổi)
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        ESP_LOGE(TAG, "Không tìm thấy OLED!"); for (;;);
    }
    oled_show_message("Booting...", "OLED OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. Khởi động thẻ SD (Không đổi)
    if (sd_mount(SD_CS) != ESP_OK) {
        ESP_LOGE(TAG, "Mount SD Card that bai!");
        oled_show_message("ERROR", "SD Card Mount Failed"); for (;;);
    }
    oled_show_message("Booting...", "SD Card OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    // 3. Nạp metadata (QUAN TRỌNG)
    // Hàm này giờ sẽ tự động TẠO VÀ LƯU menu ở "bên trong"
    sd_load_metadata(); 
    // flasher_init();

    // 4. (MỚI) LẤY MENU TỪ MODULE SD_CARD
    ESP_LOGI(TAG, "Dang lay menu tu sd_card module...");
    
    int menuLength = 0;
    const char** displayItems = sd_get_menu_display_items(menuLength);
    const char** idItems = sd_get_menu_id_items();

    if (menuLength == 0) {
        ESP_LOGE(TAG, "sd_card tra ve menu rong! Kiem tra index.txt");
        oled_show_message("ERROR", "Menu is empty!");
        for(;;);
    }

    // 5. Khởi động module menu (Siêu sạch)
    // Hàm menu_init (cũ, không sửa) nhận 2 mảng này ngon lành
    menu_init(display, displayItems, idItems, menuLength);
    
    ESP_LOGI(TAG, "Khoi dong hoan tat. Hien thi menu.");
}
/**
 * @brief HÀM LOOP CHÍNH
 */
void loop() {
    // 1. Chỉ cần gọi hàm update của menu
    int selectedIndex = menu_update();

    // 2. Kiểm tra xem có phải vừa nhấn OK không (index >= 0)
    if (selectedIndex != -1) {
        
        // 3. Lấy chuỗi ID firmware từ index đã chọn
        const char* fw_id_char = menu_get_id(selectedIndex);
        
        // Chuyển sang std::string để dùng với hàm flasher
        std::string fw_id_to_flash(fw_id_char ? fw_id_char : "");

        ESP_LOGI(TAG, "Da chon: Index %d, FW ID: %s", selectedIndex, fw_id_to_flash.c_str());

        // 4. Xử lý logic chính
        // Kiểm tra xem ID có hợp lệ và KHÁC "NULL" không
        if (!fw_id_to_flash.empty() && fw_id_to_flash != "NULL") {
            
            //Thông báo cho user biết
            ESP_LOGI(TAG, "Bat dau flash firmware: %s", fw_id_to_flash.c_str());
            std::string msg = "Flashing: " + fw_id_to_flash;
            oled_show_message("Please wait...", msg.c_str());
            // 2. Khởi động thẻ SD (Không đổi)
            if (sd_mount(SD_CS) != ESP_OK) {
                ESP_LOGE(TAG, "Mount SD Card that bai!");
                oled_show_message("ERROR", "SD Card Mount Failed"); for (;;);
            }
            oled_show_message("Booting...", "SD Card OK");

            // == GỌI HÀM FLASHER CỦA BRO ==
            // (Giả định hàm này là blocking - nó sẽ chạy cho đến khi xong)
            flasher_init();
            ESP_LOGI(TAG, "Flasher da san sang.");
            // oled_show_message("Booting...", "");
            
            if(flasher_begin_session(fw_id_to_flash) != ESP_OK){
                ESP_LOGE(TAG, "Flash that bai do loi flasher.");
                oled_show_message("Error", "Flasher failed.");
                vTaskDelay(pdMS_TO_TICKS(2000));
                // Quay lại menu
                menu_redisplay();
                return;
            }

            // Hiển thị thông báo thành công
            ESP_LOGI(TAG, "Flash hoan tat.");
            oled_show_message("Success!", "Flash complete.");
            vTaskDelay(pdMS_TO_TICKS(2000)); // Hiển thị 2s

        } else {
            // Trường hợp chọn "Exit" (có ID là "NULL") hoặc ID bị rỗng
            ESP_LOGI(TAG, "Xoa du lieu flash tren chip Target.");
            oled_show_message("Erasing Chip...", "Please wait.");
            flasher_init();
            oled_show_message("Erasing Chip...", "connected.");
            flasher_chip_erase();
            oled_show_message("Success!", "Chip erased.");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        // 5. Khởi động lại hệ thống sau khi hoàn tất
        host_system_restart();

    }
    // Thêm một chút delay để tránh vòng lặp chạy quá nhanh
    vTaskDelay(pdMS_TO_TICKS(10)); 
}

extern "C" void app_main() {
    initArduino();   // 🔹 Cực kỳ quan trọng: khởi tạo môi trường Arduino
    setup();
    while (true) {
        loop();
    }
}