/*
 * Ten Du An: ESP32 Host Flasher
 * Mo ta: Firmware quan ly nap code da nang (Offline SD + Online Git Sync)
 * Tac gia: TTP27
 * Ngay: 2025 (Refactored with OledUI Tabs)
 *
 * UI: Tabs-based voi OledUI library
 * - Tab 0: FW (Firmware list -> OK to flash)
 * - Tab 1: Tools (Sync, Monitor, Erase, Scan)
 * - Tab 2: Desc (FW list -> scroll to view description, no flash)
 * - Tab 3: Info (Version, Author, etc.)
 */

#include "Arduino.h"
#include <Wire.h>

// ============================================================
// 1. ESP-IDF & SYSTEM HEADERS
// ============================================================
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================
// 2. PROJECT MODULES
// ============================================================
#include "firmware_types.h"
#include "sd_card/sd_card.h"
#include "flasher/flasher_common.h"    // host_system_restart(), shared defines
#include "oled_ui.h"                    // OledUI Library
#include "app_actions/app_actions.h"   // High-level actions API

// ============================================================
// 3. CONSTANTS & GLOBALS
// ============================================================
static const char *TAG = "MAIN_APP";

#define SD_CS_PIN   GPIO_NUM_7
#define SDA_PIN     GPIO_NUM_8
#define SCL_PIN     GPIO_NUM_9
#define BTN_UP      GPIO_NUM_21
#define BTN_DOWN    GPIO_NUM_10
#define BTN_OK      GPIO_NUM_20
#define OLED_ADDR   0x3C

// OLED Display
Adafruit_SH1106G display(128, 64, &Wire, -1);

// OledUI Instance
OledUI ui;

// ============================================================
// TAB CONFIGURATION
// ============================================================
#define TAB_FW      0
#define TAB_TOOLS   1
#define TAB_DESC    2
#define TAB_INFO    3

const char* tabNames[] = {"FW", "Tools", "Desc", "Info"};
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
    "4. Erase Chip (ESP)",
    "5. Scan Boot",
    "6. Erase Chip (SWD)"
};
const int TOOLS_COUNT = 6;

// Tab item counts
int tabItemCounts[4] = {0, 6, 0, 0};  // FW:dynamic, Tools:6, Desc:dynamic, Info:0

// ============================================================
// UI CALLBACKS CHO APP_ACTIONS
// ============================================================

// Callback ve man hinh monitor
static void ui_monitor_draw_cb(char log_buffer[APP_ACTIONS_LOG_LINES][APP_ACTIONS_LOG_LINE_WIDTH + 1],
                                int log_write_index, bool has_activity) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    display.setCursor(0, 0);
    if (has_activity) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        display.print(" MONITOR ");
        display.setTextColor(SH110X_WHITE);
        display.println(" [RX]");
    } else {
        display.println("MONITOR [OK]=Exit");
        
    }

    display.drawLine(0, 9, 128, 9, SH110X_WHITE);

    int yPos = 11;
    for (int i = 0; i < APP_ACTIONS_LOG_LINES - 1; i++) {
        int idx = (log_write_index + i) % APP_ACTIONS_LOG_LINES;
        display.setCursor(0, yPos);
        display.println(log_buffer[idx]);
        yPos += 9;
    }

    display.display();
}

// Callback progress
static void ui_progress_cb(const char* text, int percent) {
    ui.showProgress(text, percent);
}

// Callback confirm dialog
static bool ui_confirm_cb(const char* title, const char* msg) {
    return ui.dialogConfirm(title, msg);
}

// Callback message
static void ui_message_cb(const char* title, const char* msg) {
    ui.showMessage(title, msg);
}

// Callback spinner
static void ui_spinner_cb(const char* text, int frame) {
    ui.showSpinner(text, frame);
}


// ============================================================
// TAB DRAW CALLBACK
// ============================================================
void drawTabContent(int tab, int itemIdx, int yStart) {
    int maxVisible = (ui.tabsGetHeaderHeight() > 0) ? 5 : 6;

    switch (tab) {
        case TAB_FW:  // Firmware list - OK to flash
            if (fwCount > 0 && fwDisplayItems) {
                ui.drawList(fwDisplayItems, fwCount, itemIdx, yStart, maxVisible);
                // ui.drawHint("OK:Flash");
            } else {
                ui.drawText("No Firmware", 4, yStart);
                ui.drawText("Please Sync first", 4, yStart + 12);
            }
            break;

        case TAB_TOOLS:  // Tools
            ui.drawList(toolsItems, TOOLS_COUNT, itemIdx, yStart, maxVisible);
            // ui.drawHint("OK:Run UP+DN:Tab");
            break;

        case TAB_DESC: { // Description tab - scroll to view info, no flash
            if (fwCount == 0 || !fwIdItems) {
                ui.drawText("No Firmware", 4, yStart);
                ui.drawText("Please Sync first", 4, yStart + 12);
                break;
            }

            // Get current FW metadata
            const char* fw_id = fwIdItems[itemIdx];
            auto it = g_firmware_map.find(fw_id);

            if (it == g_firmware_map.end()) {
                ui.drawText("FW not found", 4, yStart);
                break;
            }

            firmware_metadata_t& meta = it->second;

            // Line 1: Highlighted FW name (inverted style)
            char header[22];
            snprintf(header, sizeof(header), "%d.%s v%s",
                     itemIdx + 1, meta.device_type.c_str(), meta.version.c_str());

            // Draw inverted header bar
            display.fillRect(0, yStart, 128, 10, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
            display.setCursor(2, yStart + 1);
            display.print(header);
            display.setTextColor(SH110X_WHITE);

            // Line separator
            display.drawLine(0, yStart + 11, 128, yStart + 11, SH110X_WHITE);

            // Lines 2-5: Description (lazy load từ SD - không lưu RAM)
            int descY = yStart + 14;
            String descStr = sd_get_description(fw_id);
            if (descStr.length() > 0) {
                const char* desc = descStr.c_str();
                int len = descStr.length();
                char line[22];

                for (int i = 0; i < 4 && (i * 21) < len; i++) {
                    snprintf(line, sizeof(line), "%.*s", 21, desc + (i * 21));
                    ui.drawText(line, 0, descY + (i * 10));
                }
            } else {
                ui.drawText("(No description)", 4, descY);
            }

            // Hint - different from TAB_FW
            // ui.drawHint("UP/DN:Browse");
            break;
        }

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
                action_flash_firmware(fwIdItems[itemIdx]);
            }
            break;

        case TAB_TOOLS:
            switch (itemIdx) {
                case 0:  // Sync
                    action_sync_firmware();
                    host_system_restart();
                    break;
                case 1:  // Config WiFi/URL
                    action_config_wifi();
                    host_system_restart();
                    break;
                case 2:  // Monitor
                    action_monitor();
                    break;
                case 3:  // Erase
                    action_erase_chip();
                    break;
                case 4:  // Scan
                    action_scan_boot();
                    break;
                case 5:  // SWD Erase
                    action_erase_chip_swd();
                    break;
            }
            break;

        case TAB_DESC:
        case TAB_INFO:
            // Khong co hanh dong cho cac tab nay (chi xem thong tin)
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
    tabItemCounts[TAB_DESC] = fwCount;  // Same list as TAB_FW but for viewing description
    tabItemCounts[TAB_INFO] = 0;

    // Create tabs
    ui.tabsCreate(tabNames, TAB_COUNT, tabItemCounts);
    ui.tabsSetHeaderStyle(UI_HEADER_SINGLE);
    ui.tabsSetHeaderMode(UI_HEADER_ALWAYS);
    // ui.tabsSetHeaderTimeout(2000);
    ui.tabsSetDrawCallback(drawTabContent);

    // Blocking loop - tu handle header animation
    ui.tabsRun();

    // Khi OK duoc nhan, xu ly theo tab hien tai
    int tab = ui.tabsGetCurrent();
    int item = ui.tabsGetItemIndex();
    handleTabSelection(tab, item);
}

// ============================================================
// SETUP & LOOP
// ============================================================
void setup() {
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("gpio", ESP_LOG_WARN);
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

    // Init App Actions - 1 config gon gang
    app_actions_config_t actions_cfg = {
        .sd_cs_pin = SD_CS_PIN,
        .btn_ok_pin = BTN_OK,
        .btn_up_pin = BTN_UP,
        .btn_down_pin = BTN_DOWN,
        .on_progress = ui_progress_cb,
        .on_confirm = ui_confirm_cb,
        .on_message = ui_message_cb,
        .on_spinner = ui_spinner_cb,
        .on_monitor_draw = ui_monitor_draw_cb
    };
    app_actions_init(&actions_cfg);

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
