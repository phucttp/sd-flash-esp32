/*
 * Ten Du An: ESP32 Host Flasher
 * Mo ta: Firmware quan ly nap code da nang (Offline SD + Online Git Sync)
 * Tac gia: TTP27
 * Ngay: 2025 (Refactored with OledUI Tabs)
 *
 * UI: Tabs-based voi OledUI library
 * - Tab 0: FW (Firmware list)
 * - Tab 1: Tools (Sync, Monitor, Erase, Scan)
 * - Tab 2: Status (Real-time info)
 * - Tab 3: Info (Version, Author, etc.)
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
// 2. PROJECT MODULES
// ============================================================
#include "firmware_types.h"
#include "sd_card/sd_card.h"
#include "flasher/flasher.h"
#include "oled_ui.h"                    // OledUI Library
#include "wifi_config/wifi_config.h"
#include "ota_downloader/ota_downloader.h"
#include "metadata_parser/metadata_parser.h"
#include "sync_engine/sync_engine.h"

// ============================================================
// 3. CONSTANTS & GLOBALS
// ============================================================
static const char *TAG = "MAIN_APP";
bool force_delete = false;

#define SD_CS_PIN   GPIO_NUM_7
#define SDA_PIN     GPIO_NUM_8
#define SCL_PIN     GPIO_NUM_9
#define BTN_UP      GPIO_NUM_21
#define BTN_DOWN    GPIO_NUM_10
#define BTN_OK      GPIO_NUM_20
#define OLED_ADDR   0x3C
#define BUF_LEN     128

// OLED Display
Adafruit_SH1106G display(128, 64, &Wire, -1);

// OledUI Instance
OledUI ui;

// Buffer
static uint8_t buf[BUF_LEN] = {0};

// ============================================================
// TAB CONFIGURATION
// ============================================================
#define TAB_FW      0
#define TAB_TOOLS   1
#define TAB_STATUS  2
#define TAB_INFO    3

const char* tabNames[] = {"FW", "Tools", "Status", "Info"};
const int TAB_COUNT = 4;

// Firmware items (loaded from SD)
const char** fwDisplayItems = nullptr;
const char** fwIdItems = nullptr;
int fwCount = 0;

// Tools items
const char* toolsItems[] = {
    "1. Sync Firmware",
    "2. Config WiFi/URL",
    "3. Monitor/Test",
    "4. Erase Chip",
    "5. Scan Boot"
};
const int TOOLS_COUNT = 5;

// Tab item counts
int tabItemCounts[4] = {0, 5, 0, 0};  // FW:dynamic, Tools:5, Status:0, Info:0

// Status data (real-time)
volatile int statusProgress = 0;
volatile const char* statusText = "Ready";
volatile bool isFlashing = false;

// ============================================================
// MONITOR MODE
// ============================================================
#define LOG_LINES 7
#define LOG_LINE_WIDTH 21

static char logBuffer[LOG_LINES][LOG_LINE_WIDTH + 1];
static int logWriteIndex = 0;
static char uartLineBuffer[256];
static int uartLinePos = 0;

static void log_buffer_init() {
    for (int i = 0; i < LOG_LINES; i++) {
        logBuffer[i][0] = '\0';
    }
    logWriteIndex = 0;
    uartLinePos = 0;
    uartLineBuffer[0] = '\0';
}

static void log_buffer_add_line(const char* line) {
    strncpy(logBuffer[logWriteIndex], line, LOG_LINE_WIDTH);
    logBuffer[logWriteIndex][LOG_LINE_WIDTH] = '\0';
    logWriteIndex = (logWriteIndex + 1) % LOG_LINES;
}

static void log_process_uart_data(const char* data, int len) {
    for (int i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n' || c == '\r') {
            if (uartLinePos > 0) {
                uartLineBuffer[uartLinePos] = '\0';
                log_buffer_add_line(uartLineBuffer);
                uartLinePos = 0;
            }
        } else if (uartLinePos < (int)sizeof(uartLineBuffer) - 1) {
            uartLineBuffer[uartLinePos++] = c;
        }
    }
}

static void draw_monitor_log_screen(bool hasActivity) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    display.setCursor(0, 0);
    if (hasActivity) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        display.print(" MONITOR ");
        display.setTextColor(SH110X_WHITE);
        display.println(" [RX]");
    } else {
        display.println("MONITOR [OK]=Exit");
    }

    display.drawLine(0, 9, 128, 9, SH110X_WHITE);

    int yPos = 11;
    for (int i = 0; i < LOG_LINES - 1; i++) {
        int idx = (logWriteIndex + i) % LOG_LINES;
        display.setCursor(0, yPos);
        display.println(logBuffer[idx]);
        yPos += 9;
    }

    display.display();
}

static void run_monitor_mode() {
    ESP_LOGI(TAG, ">>> START MONITOR MODE");
    ui.showMessage("Starting Monitor", "Waiting for data...");
    vTaskDelay(pdMS_TO_TICKS(500));

    flasher_init();
    uart_flush_input(UART_NUM_1);
    uart_set_baudrate(UART_NUM_1, 115200);

    log_buffer_init();
    log_buffer_add_line("Waiting for Target...");
    log_buffer_add_line("Baudrate: 115200");
    log_buffer_add_line("--------------------");

    bool hasActivity = false;
    unsigned long lastActivityTime = 0;
    unsigned long lastDisplayUpdate = 0;

    draw_monitor_log_screen(false);

    while (1) {
        if (digitalRead(BTN_OK) == LOW) {
            ESP_LOGI(TAG, ">>> EXIT MONITOR");
            ui.showMessage("Exiting...", "");
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        }

        int rxBytes = uart_read_bytes(UART_NUM_1, buf, BUF_LEN - 1, 20 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            buf[rxBytes] = 0;
            ESP_LOGI("TARGET", "%s", buf);
            log_process_uart_data((char*)buf, rxBytes);
            hasActivity = true;
            lastActivityTime = millis();
        }

        unsigned long now = millis();
        if (now - lastDisplayUpdate >= 100) {
            lastDisplayUpdate = now;
            if (now - lastActivityTime > 1000) {
                hasActivity = false;
            }
            draw_monitor_log_screen(hasActivity);
        }

        vTaskDelay(10);
    }
}

// ============================================================
// SYNC PROCESS
// ============================================================
static void run_sync_process() {
    ESP_LOGI(TAG, ">>> START SYNC PROCESS");

    if (!ui.dialogConfirm("Sync FW?", "Connect to WiFi?")) {
        return;
    }

    ui.showMessage("Connecting...", "Please wait");

    if (wifi_config_connect()) {
        ESP_LOGI(TAG, "WiFi Connected. Starting Sync Engine...");
        sync_engine_run(force_delete);
        vTaskDelay(2000);
    }

    wifi_config_stop();
}

// ============================================================
// CHIP ERASE
// ============================================================
static void run_chip_erase() {
    ESP_LOGW(TAG, ">>> ERASE TARGET CHIP");

    if (!ui.dialogConfirm("Erase Chip?", "All data will be lost!")) {
        return;
    }

    ui.showProgress("Erasing chip...", 0);
    flasher_init();

    for (int i = 0; i <= 50; i += 10) {
        ui.showProgress("Erasing chip...", i);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (flasher_chip_erase() == ESP_OK) {
        ui.showProgress("Erasing chip...", 100);
        vTaskDelay(pdMS_TO_TICKS(500));
        ui.showMessage("SUCCESS!", "Chip Erased.");
    } else {
        ui.showMessage("Error", "Erase Failed!");
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
}

// ============================================================
// SCAN BOOT
// ============================================================
static void run_scan_boot() {
    ESP_LOGI(TAG, ">>> SCAN BOOT MODE");

    for (int i = 0; i < 30; i++) {
        ui.showSpinner("Scanning combos...", i);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    flasher_init();
    int combo_index = flasher_scan_and_save_combo();

    if (combo_index >= 0) {
        char msg[24];
        snprintf(msg, sizeof(msg), "Saved Combo #%d", combo_index);
        ui.showMessage("SUCCESS!", msg);
    } else {
        ui.showMessage("FAILED!", "No combo found");
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
}

// ============================================================
// FLASH FIRMWARE
// ============================================================
static void run_flash_fw(const char* fw_id) {
    ESP_LOGI(TAG, ">>> FLASH FW: %s", fw_id);

    char msg[32];
    snprintf(msg, sizeof(msg), "Flash %s?", fw_id);

    if (!ui.dialogConfirm("Confirm", msg)) {
        return;
    }

    isFlashing = true;
    statusText = "Connecting...";
    statusProgress = 0;

    if (sd_mount(SD_CS_PIN) != ESP_OK) {
        ui.showMessage("Error", "SD Card Lost!");
        vTaskDelay(2000);
        isFlashing = false;
        return;
    }

    flasher_init();

    // Update status
    statusText = "Flashing...";
    for (int i = 0; i <= 30; i += 5) {
        statusProgress = i;
        ui.showProgress("Connecting...", i);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    std::string fw_id_str(fw_id);
    if (flasher_begin_session(fw_id_str) == ESP_OK) {
        statusProgress = 100;
        statusText = "Done";
        ui.showMessage("Done!", "Flash Complete");
        vTaskDelay(pdMS_TO_TICKS(1500));
        // Reset host để giải phóng bộ nhớ sau khi flash
        host_system_restart();
    } else {
        statusText = "Error";
        ui.showMessage("Error", "Flash Failed!");
        isFlashing = false;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ============================================================
// TAB DRAW CALLBACK
// ============================================================
void drawTabContent(int tab, int itemIdx, int yStart) {
    int maxVisible = (ui.tabsGetHeaderHeight() > 0) ? 5 : 6;

    switch (tab) {
        case TAB_FW:  // Firmware list
            if (fwCount > 0 && fwDisplayItems) {
                ui.drawList(fwDisplayItems, fwCount, itemIdx, yStart, maxVisible);
                // ui.drawHint("OK:Flash UP+DN:Tab");
            } else {
                ui.drawText("No Firmware", 4, yStart);
                ui.drawText("Please Sync first", 4, yStart + 12);
                // ui.drawHint("UP+DN:Tab");
            }
            break;

        case TAB_TOOLS:  // Tools
            ui.drawList(toolsItems, TOOLS_COUNT, itemIdx, yStart, maxVisible);
            // ui.drawHint("OK:Run UP+DN:Tab");
            break;

        case TAB_STATUS:  // Status (real-time)
            ui.drawText("System Status:", 4, yStart);
            ui.drawKeyValue("State", (char*)statusText, yStart + 12);
            ui.drawProgressBar(statusProgress, yStart + 26, 10);

            char pct[8];
            snprintf(pct, sizeof(pct), "%d%%", statusProgress);
            ui.drawTextCenter(pct, yStart + 40);

            if (isFlashing) {
                ui.drawHint("Flashing in progress");
            } else {
                // ui.drawHint("UP+DN:Tab OK:Exit");
            }
            break;

        case TAB_INFO:  // Info
            ui.drawKeyValue("Version", "1.0.0", yStart);
            ui.drawKeyValue("Author", "TTP27", yStart + 12);
            ui.drawKeyValue("Build", "250131", yStart + 24);
            ui.drawKeyValue("Chip", "ESP32-C3", yStart + 36);
            // ui.drawHint("UP+DN:Tab OK:Exit");
            break;
    }
}

// ============================================================
// HANDLE TAB SELECTION
// ============================================================
void handleTabSelection(int tab, int itemIdx) {
    switch (tab) {
        case TAB_FW:
            if (fwCount > 0 && fwIdItems && itemIdx < fwCount) {
                run_flash_fw(fwIdItems[itemIdx]);
            }
            break;

        case TAB_TOOLS:
            switch (itemIdx) {
                case 0:  // Sync
                    run_sync_process();
                    host_system_restart();
                    break;
                case 1:  // Config WiFi/URL
                    wifi_config_force_portal();
                    host_system_restart();
                    break;
                case 2:  // Monitor
                    run_monitor_mode();
                    break;
                case 3:  // Erase
                    run_chip_erase();
                    break;
                case 4:  // Scan
                    run_scan_boot();
                    break;
            }
            break;

        case TAB_STATUS:
        case TAB_INFO:
            // No action for these tabs
            break;
    }
}

// ============================================================
// MAIN TABS LOOP
// ============================================================
void runTabsUI() {
    // Update item counts
    tabItemCounts[TAB_FW] = fwCount;
    tabItemCounts[TAB_TOOLS] = TOOLS_COUNT;
    tabItemCounts[TAB_STATUS] = 0;
    tabItemCounts[TAB_INFO] = 0;

    // Create tabs
    ui.tabsCreate(tabNames, TAB_COUNT, tabItemCounts);
    ui.tabsSetHeaderStyle(UI_HEADER_SINGLE);
    ui.tabsSetHeaderMode(UI_HEADER_ALWAYS);
    ui.tabsSetDrawCallback(drawTabContent);

    bool running = true;
    bool needRedraw = true;

    while (running) {
        int result = ui.tabsUpdate();

        if (result == UI_TAB_SWITCHED || result == UI_ITEM_CHANGED) {
            needRedraw = true;
        }
        else if (result == UI_EXIT) {
            // Handle selection based on current tab
            int currentTab = ui.tabsGetCurrent();
            int itemIdx = ui.tabsGetItemIndex();

            // For Status and Info tabs, OK means exit
            if (currentTab == TAB_STATUS || currentTab == TAB_INFO) {
                // Just continue, don't exit the whole UI
                needRedraw = true;
            } else {
                // For FW and Tools, handle selection
                handleTabSelection(currentTab, itemIdx);
                needRedraw = true;
            }
        }

        // Redraw if needed
        if (needRedraw) {
            display.clearDisplay();

            int headerH = ui.tabsGetHeaderHeight();
            if (headerH > 0) {
                ui.tabsDraw();
            }

            int yStart = 4 + headerH;
            int currentTab = ui.tabsGetCurrent();
            int itemIdx = ui.tabsGetItemIndex();
            drawTabContent(currentTab, itemIdx, yStart);

            display.display();
            needRedraw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================
// SETUP & LOOP
// ============================================================
void setup() {
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "--- SYSTEM START ---");

    // Init I2C & OLED
    Wire.begin(SDA_PIN, SCL_PIN);
    if (!display.begin(OLED_ADDR, true)) {
        ESP_LOGE(TAG, "OLED Init Failed");
        for(;;);
    }

    // Init OledUI
    ui.begin(&display, BTN_UP, BTN_DOWN, BTN_OK);
    ui.showMessage("Booting...", "Init Hardware");

    // Init SD Card
    if (sd_mount(SD_CS_PIN) != ESP_OK) {
        ui.showMessage("Error", "No SD Card");
        for(;;);
    }

    // Load Firmware Data
    sd_load_metadata();

    // Get menu items
    fwDisplayItems = sd_get_menu_display_items(fwCount);
    fwIdItems = sd_get_menu_id_items();

    if (fwCount == 0) {
        ESP_LOGW(TAG, "No firmware found on SD");
    }

    // Splash screen
    ui.showMessage("FlashPorter", "Universal Flasher");
    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(TAG, "Ready! FW count: %d", fwCount);
}

void loop() {
    runTabsUI();
}

// ============================================================
// ESP-IDF ENTRY POINT
// ============================================================
extern "C" void app_main() {
    initArduino();
    setup();
    while (true) {
        loop();
    }
}
