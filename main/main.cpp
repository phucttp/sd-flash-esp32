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
// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);  // OLD
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);     // NEW: SH1106G

// Buffer tạm
static uint8_t buf[BUF_LEN] = {0};

// ============================================================
// 4. INTERNAL HELPER FUNCTIONS (HÀM XỬ LÝ RIÊNG BIỆT)
// ============================================================

// ============================================================
// MONITOR MODE - Hiển thị Log từ Target lên OLED
// ============================================================

#define LOG_LINES 7          // Số dòng log hiển thị (SH1106G 64px / 9px per line)
#define LOG_LINE_WIDTH 21    // Ký tự tối đa mỗi dòng (128px / 6px per char)

// Buffer lưu các dòng log để hiển thị
static char logBuffer[LOG_LINES][LOG_LINE_WIDTH + 1];
static int logWriteIndex = 0;  // Dòng tiếp theo sẽ ghi

// Buffer tích lũy dữ liệu UART (để ghép các chunk thành dòng hoàn chỉnh)
static char uartLineBuffer[256];
static int uartLinePos = 0;

/**
 * @brief Khởi tạo log buffer
 */
static void log_buffer_init() {
    for (int i = 0; i < LOG_LINES; i++) {
        logBuffer[i][0] = '\0';
    }
    logWriteIndex = 0;
    uartLinePos = 0;
    uartLineBuffer[0] = '\0';
}

/**
 * @brief Thêm một dòng log vào buffer (cuộn lên nếu đầy)
 */
static void log_buffer_add_line(const char* line) {
    // Copy dòng mới vào buffer (cắt nếu quá dài)
    strncpy(logBuffer[logWriteIndex], line, LOG_LINE_WIDTH);
    logBuffer[logWriteIndex][LOG_LINE_WIDTH] = '\0';

    // Di chuyển index (vòng tròn)
    logWriteIndex = (logWriteIndex + 1) % LOG_LINES;
}

/**
 * @brief Xử lý dữ liệu UART, tách thành từng dòng
 */
static void log_process_uart_data(const char* data, int len) {
    for (int i = 0; i < len; i++) {
        char c = data[i];

        if (c == '\n' || c == '\r') {
            // Kết thúc dòng -> thêm vào log buffer
            if (uartLinePos > 0) {
                uartLineBuffer[uartLinePos] = '\0';
                log_buffer_add_line(uartLineBuffer);
                uartLinePos = 0;
            }
        } else if (uartLinePos < (int)sizeof(uartLineBuffer) - 1) {
            // Thêm ký tự vào buffer dòng hiện tại
            uartLineBuffer[uartLinePos++] = c;
        }
    }
}

/**
 * @brief Vẽ màn hình Monitor với log scrolling
 */
static void draw_monitor_log_screen(bool hasActivity) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    // Header: MONITOR + status indicator
    display.setCursor(0, 0);
    if (hasActivity) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        display.print(" MONITOR ");
        display.setTextColor(SH110X_WHITE);
        display.println(" [RX]");
    } else {
        display.println("MONITOR [OK]=Exit");
    }

    // Đường kẻ ngang
    display.drawLine(0, 9, SCREEN_WIDTH, 9, SH110X_WHITE);

    // Hiển thị các dòng log (từ cũ đến mới)
    int yPos = 11;
    for (int i = 0; i < LOG_LINES - 1; i++) {  // -1 vì header chiếm 1 dòng
        // Tính index thực tế (bắt đầu từ dòng cũ nhất)
        int idx = (logWriteIndex + i) % LOG_LINES;

        display.setCursor(0, yPos);
        display.println(logBuffer[idx]);
        yPos += 9;  // 8px text + 1px spacing
    }

    display.display();
}

/**
 * @brief Chế độ Monitor: Hiển thị log từ Target lên OLED
 */
static void run_monitor_mode() {
    ESP_LOGI(TAG, ">>> START MONITOR MODE");
    oled_show_message("Starting Monitor", "Waiting for data...");
    vTaskDelay(pdMS_TO_TICKS(500));

    flasher_init();
    uart_flush_input(UART_NUM_1);
    uart_set_baudrate(UART_NUM_1, 115200);

    // Khởi tạo log buffer
    log_buffer_init();

    // Thêm dòng hướng dẫn ban đầu
    log_buffer_add_line("Waiting for Target...");
    log_buffer_add_line("Baudrate: 115200");
    log_buffer_add_line("--------------------");

    bool hasActivity = false;
    unsigned long lastActivityTime = 0;
    unsigned long lastDisplayUpdate = 0;
    const unsigned long DISPLAY_UPDATE_INTERVAL = 100;
    const unsigned long ACTIVITY_TIMEOUT = 1000;

    // Vẽ màn hình ban đầu
    draw_monitor_log_screen(false);

    while (1) {
        // Kiểm tra nút thoát
        if (digitalRead(BTN_OK) == LOW) {
            ESP_LOGI(TAG, ">>> EXIT MONITOR");
            oled_show_message("Exiting...", "");
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        }

        // Đọc UART từ Target
        int rxBytes = uart_read_bytes(UART_NUM_1, buf, BUF_LEN - 1, 20 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            buf[rxBytes] = 0;

            // Log ra serial (để debug trên PC nếu cần)
            ESP_LOGI("TARGET", "%s", buf);

            // Xử lý và thêm vào log buffer để hiển thị OLED
            log_process_uart_data((char*)buf, rxBytes);

            hasActivity = true;
            lastActivityTime = millis();
        }

        // Cập nhật màn hình định kỳ
        unsigned long now = millis();
        if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
            lastDisplayUpdate = now;

            // Kiểm tra timeout activity
            if (now - lastActivityTime > ACTIVITY_TIMEOUT) {
                hasActivity = false;
            }

            draw_monitor_log_screen(hasActivity);
        }

        vTaskDelay(10);
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

// ============================================================
// TOOLS MENU - Menu ẩn (UP+DOWN 3s)
// ============================================================

#define TOOLS_SYNC    0
#define TOOLS_MONITOR 1
#define TOOLS_ERASE   2
#define TOOLS_SCAN    3
#define TOOLS_BACK    4

static const char* toolsMenuItems[] = {
    "1. Sync Firmware",
    "2. Monitor/Test",
    "3. Erase Chip",
    "4. Scan Boot",
    "5. [Back]"
};
static const int toolsMenuLen = 5;

/**
 * @brief Hiển thị và xử lý Tools Menu (với scrolling)
 * @return Tool đã chọn (TOOLS_xxx) hoặc -1 nếu back/cancel
 */
static int run_tools_menu() {
    ESP_LOGI(TAG, ">>> OPEN TOOLS MENU");

    int currentTool = 0;
    int topIndex = 0;  // Item đầu tiên hiển thị
    const int maxVisible = 6;  // Số dòng tối đa (trừ header)

    unsigned long lastDebounce = 0;
    unsigned long holdStartTime = 0;
    bool isHolding = false;
    const unsigned long debounceDelay = 200;
    const unsigned long fastScrollDelay = 80;
    const unsigned long holdThreshold = 500;

    // Vẽ menu với scrolling
    auto drawToolsMenu = [&]() {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);

        // Header (fixed)
        display.fillRect(0, 0, SCREEN_WIDTH, 10, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        display.setCursor(30, 1);
        display.println("TOOLS MENU");
        display.setTextColor(SH110X_WHITE);

        // Menu items (scrollable)
        for (int i = 0; i < maxVisible; i++) {
            int itemIndex = topIndex + i;
            if (itemIndex >= toolsMenuLen) break;

            int yPos = 12 + i * 9;  // Bắt đầu sau header

            if (itemIndex == currentTool) {
                display.fillRect(0, yPos - 1, SCREEN_WIDTH, 9, SH110X_WHITE);
                display.setTextColor(SH110X_BLACK, SH110X_WHITE);
            } else {
                display.setTextColor(SH110X_WHITE, SH110X_BLACK);
            }
            display.setCursor(4, yPos);
            display.println(toolsMenuItems[itemIndex]);
        }

        // Position indicator (nếu có scroll)
        if (toolsMenuLen > maxVisible) {
            char posStr[12];
            snprintf(posStr, sizeof(posStr), "%d/%d", currentTool + 1, toolsMenuLen);
            int textWidth = strlen(posStr) * 6;
            int xPos = SCREEN_WIDTH - textWidth - 2;
            int yPos = SCREEN_HEIGHT - 8;
            display.fillRect(xPos - 2, yPos, textWidth + 4, 8, SH110X_BLACK);
            display.setTextColor(SH110X_WHITE);
            display.setCursor(xPos, yPos);
            display.print(posStr);
        }

        display.display();
    };

    drawToolsMenu();

    while (1) {
        bool btnUp = (digitalRead(BTN_UP) == LOW);
        bool btnDown = (digitalRead(BTN_DOWN) == LOW);
        bool btnOk = (digitalRead(BTN_OK) == LOW);

        // Hold detection cho fast scroll
        if (btnUp || btnDown) {
            if (!isHolding) {
                holdStartTime = millis();
                isHolding = true;
            }
        } else {
            isHolding = false;
            holdStartTime = 0;
        }

        // Tính delay
        unsigned long currentDelay = debounceDelay;
        if (isHolding && (millis() - holdStartTime > holdThreshold)) {
            currentDelay = fastScrollDelay;
        }

        if (millis() - lastDebounce < currentDelay) {
            vTaskDelay(10);
            continue;
        }

        bool menuChanged = false;

        if (btnUp) {
            currentTool--;
            if (currentTool < 0) {
                currentTool = toolsMenuLen - 1;
                topIndex = toolsMenuLen - maxVisible;
                if (topIndex < 0) topIndex = 0;
            }
            if (currentTool < topIndex) {
                topIndex = currentTool;
            }
            menuChanged = true;
        }
        else if (btnDown) {
            currentTool++;
            if (currentTool >= toolsMenuLen) {
                currentTool = 0;
                topIndex = 0;
            }
            if (currentTool >= (topIndex + maxVisible)) {
                topIndex = currentTool - maxVisible + 1;
            }
            menuChanged = true;
        }
        else if (btnOk) {
            ESP_LOGI(TAG, "Tools selected: %d", currentTool);

            // Chờ thả nút OK
            while (digitalRead(BTN_OK) == LOW) {
                vTaskDelay(10);
            }
            vTaskDelay(pdMS_TO_TICKS(100));

            return currentTool;
        }

        if (menuChanged) {
            drawToolsMenu();
            lastDebounce = millis();
        }

        vTaskDelay(10);
    }
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
 * @brief Chế độ Scan Boot - Quét và lưu combo kết nối
 */
static void run_scan_boot() {
    ESP_LOGI(TAG, ">>> SCAN BOOT MODE");
    oled_show_message("Scan Boot", "Scanning...");
    vTaskDelay(pdMS_TO_TICKS(500));

    flasher_init();

    // Gọi hàm scan và lưu combo working
    int combo_index = flasher_scan_and_save_combo();

    if (combo_index >= 0) {
        char msg[24];
        snprintf(msg, sizeof(msg), "Saved Combo #%d", combo_index);
        oled_show_message("SUCCESS!", msg);
    } else {
        oled_show_message("FAILED!", "No combo found");
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
    // if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {  // OLD
    if (!display.begin(OLED_I2C_ADDR, true)) {                      // NEW: SH1106G (addr, reset)
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

    // [FIX] Menu giờ luôn có ít nhất 3 mục (Monitor, SyncFW, Erase Chip)
    // Chỉ log warning thay vì crash
    if (menuLen == 0) {
        ESP_LOGW(TAG, "Menu Empty - this should not happen!");
        oled_show_message("Warning", "No Menu Items");
        vTaskDelay(pdMS_TO_TICKS(2000));
        // Thử load lại thay vì treo máy
        sd_load_metadata();
        dispItems = sd_get_menu_display_items(menuLen);
        idItems = sd_get_menu_id_items();
    }

    menu_init(display, dispItems, idItems, menuLen);
    ESP_LOGI(TAG, "Ready!");
}

void loop() {
    // [1] Lắng nghe Menu
    int selectedIndex = menu_update();

    // ============================================================
    // [2] XỬ LÝ GESTURE: UP+DOWN 3s -> Tools Menu
    // ============================================================
    if (selectedIndex == MENU_TOOLS_GESTURE) {
        int tool = run_tools_menu();

        switch (tool) {
            case TOOLS_SYNC:
                vTaskDelay(pdMS_TO_TICKS(300));
                run_sync_process();
                host_system_restart();
                break;

            case TOOLS_MONITOR:
                run_monitor_mode();
                menu_redisplay();  // Quay lại menu chính
                break;

            case TOOLS_ERASE:
                run_chip_erase();
                menu_redisplay();
                break;

            case TOOLS_SCAN:
                run_scan_boot();
                menu_redisplay();
                break;

            case TOOLS_BACK:
            default:
                menu_redisplay();  // Quay lại menu chính
                break;
        }
        return;
    }

    // ============================================================
    // [3] XỬ LÝ CHỌN FIRMWARE (Index >= 0)
    // ============================================================
    if (selectedIndex >= 0) {
        const char* fw_id_char = menu_get_id(selectedIndex);
        std::string fw_id(fw_id_char ? fw_id_char : "");

        ESP_LOGI(TAG, "Selected FW: %s", fw_id.c_str());

        // Kiểm tra trường hợp menu rỗng
        if (fw_id.empty() || fw_id == "NO_FW") {
            oled_show_message("No Firmware", "Please Sync First");
            vTaskDelay(pdMS_TO_TICKS(2000));
            menu_redisplay();
            return;
        }

        // --- NẠP FIRMWARE ---
        run_flash_fw(fw_id);

        // Sau khi nạp xong -> Reset để refresh menu
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