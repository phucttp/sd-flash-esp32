/**
 * @file main.cpp
 * @brief Điểm khởi động firmware ESP MultiFlasher — khởi tạo hệ thống và vòng lặp giao diện chính.
 *
 * Chức năng chính:
 *   - Khởi tạo SD card, màn hình OLED, WiFi và các module flasher khi boot
 *   - Điều phối giao diện dạng tab qua thư viện OledUI:
 *       Tab 0 (FW)    — danh sách firmware, chọn để nạp ngay
 *       Tab 1 (Tools) — đồng bộ git, monitor UART, xóa chip, quét thiết bị
 *       Tab 2 (Desc)  — xem mô tả firmware, chọn để nạp
 *       Tab 3 (Hist)  — lịch sử các lần nạp gần nhất
 *       Tab 4 (Info)  — thông tin phiên bản, tác giả
 *   - Lưu và khôi phục trạng thái giao diện (tab + item đang chọn) qua ui_state
 *   - Định nghĩa callback cho từng hành động người dùng trên UI
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
#include "ui_state/ui_state.h"         // UI state persistence
#include "wifi_config/wifi_config.h"   // wifi_config_connect() for NetFlash auto-reconnect
#include "net_server/net_server.h"     // HTTP API server + mDNS for NetFlash

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
#define TAB_HIST    3
#define TAB_INFO    4

const char* tabNames[] = {"FW", "Tools", "Desc", "Hist", "Info"};
const int TAB_COUNT = 5;

// Firmware items (loaded from SD)
const char** fwDisplayItems = nullptr;
const char** fwIdItems = nullptr;
int fwCount = 0;

// History items (loaded from SD after metadata)
const char** histDisplayItems = nullptr;
const char** histIdItems = nullptr;
int histCount = 0;

// Tools items
static char s_netflash_label[22] = "7. NetFlash: OFF";
const char* toolsItems[] = {
    "1. Sync Firmware",
    "2. Config WiFi/URL",
    "3. Monitor/Test",
    "4. Erase Chip (ESP)",
    "5. Scan Boot",
    "6. Erase Chip (SWD)",
    s_netflash_label
};
const int TOOLS_COUNT = 7;

// Tab item counts
int tabItemCounts[5] = {0, 7, 0, 0, 0};  // FW:dynamic, Tools:7, Desc:dynamic, Hist:dynamic, Info:0

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

        case TAB_DESC: { // Description tab - OK to flash (same firmware list as FW tab)
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

        case TAB_HIST:  // Flash history — #N DevType vVer, most recent first
            if (histCount > 0 && histDisplayItems) {
                ui.drawList(histDisplayItems, histCount, itemIdx, yStart, maxVisible);
            } else {
                ui.drawText("No history yet", 4, yStart);
                ui.drawText("Flash to start", 4, yStart + 12);
            }
            break;

        case TAB_INFO:  // Info
            ui.drawKeyValue("Version", "1.1.0", yStart);
            ui.drawKeyValue("Author", "TTP27", yStart + 12);
            ui.drawKeyValue("Build", "260216", yStart + 24);
            ui.drawKeyValue("Chip", "ESP32-C3", yStart + 36);
            // ui.drawHint("UP+DN:Tab OK:Exit");
            break;
    }
}

// ============================================================
// HANDLE TAB SELECTION
// ============================================================
void handleTabSelection(int tab, int itemIdx) {
    // Helper: Save UI state + restart with animation
    // State save + animation được xử lý trong host_system_restart() (flasher_common.cpp)
    auto saveAndRestart = [&](const char* item_id) {
        host_system_restart(tab, item_id);  // Save state, show animation, restart
    };

    switch (tab) {
        case TAB_FW:
            if (fwCount > 0 && fwIdItems && itemIdx < fwCount) {
                const char* fw_id = fwIdItems[itemIdx];
                esp_err_t ret = action_flash_firmware(fw_id);
                if (ret == ESP_OK) {
                    sd_history_add(fw_id);
                    saveAndRestart(fw_id);
                }
            }
            break;

        case TAB_TOOLS:
            switch (itemIdx) {
                case 0: {  // Sync
                    esp_err_t ret = action_sync_firmware();
                    if (ret == ESP_OK) {
                        saveAndRestart("0");
                    }
                    break;
                }
                case 1: {  // Config WiFi/URL
                    esp_err_t ret = action_config_wifi();
                    if (ret == ESP_OK) {
                        saveAndRestart("1");
                    }
                    break;
                }
                case 2:  // Monitor
                    action_monitor();
                    saveAndRestart("2");
                    break;

                case 3: {  // Erase Chip (ESP)
                    esp_err_t ret = action_erase_chip();
                    if (ret == ESP_OK) {
                        saveAndRestart("3");
                    }
                    break;
                }
                case 4:  // Scan Boot
                    action_scan_boot();
                    saveAndRestart("4");
                    break;

                case 5: {  // Erase Chip (SWD)
                    esp_err_t ret = action_erase_chip_swd();
                    if (ret == ESP_OK) {
                        saveAndRestart("5");
                    }
                    break;
                }
                case 6: {  // NetFlash toggle
                    action_toggle_netflash();
                    snprintf(s_netflash_label, sizeof(s_netflash_label),
                             "7. NetFlash: %s", action_netflash_is_enabled() ? "ON " : "OFF");
                    // Start or stop HTTP server along with WiFi state
                    if (action_netflash_is_enabled()) {
                        net_server_start();
                    } else {
                        net_server_stop();
                    }
                    break;
                }
            }
            break;

        case TAB_DESC:
            if (fwCount > 0 && fwIdItems && itemIdx < fwCount) {
                const char* fw_id = fwIdItems[itemIdx];
                esp_err_t ret = action_flash_firmware(fw_id);
                if (ret == ESP_OK) {
                    sd_history_add(fw_id);
                    saveAndRestart(fw_id);
                }
            }
            break;

        case TAB_HIST:
            if (histCount > 0 && histIdItems && itemIdx < histCount) {
                const char* fw_id = histIdItems[itemIdx];
                esp_err_t ret = action_flash_firmware(fw_id);
                if (ret == ESP_OK) {
                    sd_history_add(fw_id);
                    saveAndRestart(fw_id);
                }
            }
            break;

        case TAB_INFO:
            break;
    }
}

// ============================================================
// MAIN TABS LOOP
// ============================================================
void runTabsUI() {
    // Update item counts
    tabItemCounts[TAB_FW]    = fwCount;
    tabItemCounts[TAB_TOOLS] = TOOLS_COUNT;
    tabItemCounts[TAB_DESC]  = fwCount;
    tabItemCounts[TAB_HIST]  = histCount;
    tabItemCounts[TAB_INFO]  = 0;

    // Create tabs
    ui.tabsCreate(tabNames, TAB_COUNT, tabItemCounts);
    ui.tabsSetHeaderStyle(UI_HEADER_SINGLE);
    ui.tabsSetHeaderMode(UI_HEADER_ALWAYS);
    // ui.tabsSetHeaderTimeout(2000);
    ui.tabsSetDrawCallback(drawTabContent);

    // --- RESTORE UI STATE (sau khi restart) ---
    // Nếu có state file → restore tab + item, xóa file sau đó
    ESP_LOGI(TAG, "Checking for UI state...");

    if (!ui_state_exists()) {
        ESP_LOGI(TAG, "No state file found - starting fresh");
    } else {
        ESP_LOGI(TAG, "State file exists - attempting restore");

        int saved_tab = -1;
        char saved_item[UI_STATE_ITEM_ID_MAX_LEN];
        esp_err_t load_ret = ui_state_load(&saved_tab, saved_item, sizeof(saved_item));

        if (load_ret == ESP_OK) {
            ESP_LOGI(TAG, "✓ State loaded: tab=%d, item=%s", saved_tab, saved_item);

            // Set tab BEFORE calling tabsRun
            ui.tabsSetCurrent(saved_tab);
            ESP_LOGI(TAG, "✓ Tab set to %d", saved_tab);

            // Set item index (tìm item ID trong list)
            if (saved_tab == TAB_FW || saved_tab == TAB_DESC) {
                // FW/Desc tabs: tìm item theo fw_id
                bool found = false;
                for (int i = 0; i < fwCount; i++) {
                    if (fwIdItems && strcmp(fwIdItems[i], saved_item) == 0) {
                        ui.tabsSetItemIndex(i);
                        ESP_LOGI(TAG, "✓ FW item restored: %s (index %d)", saved_item, i);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ESP_LOGW(TAG, "✗ FW item '%s' not found in list", saved_item);
                }
            } else if (saved_tab == TAB_HIST) {
                // History tab: tìm fw_id trong histIdItems
                bool found = false;
                for (int i = 0; i < histCount; i++) {
                    if (histIdItems && strcmp(histIdItems[i], saved_item) == 0) {
                        ui.tabsSetItemIndex(i);
                        ESP_LOGI(TAG, "✓ Hist item restored: %s (index %d)", saved_item, i);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ESP_LOGW(TAG, "✗ Hist item '%s' not found", saved_item);
                }
            } else if (saved_tab == TAB_TOOLS) {
                // Tools tab: item_id là số (0-5)
                int tool_idx = atoi(saved_item);
                if (tool_idx >= 0 && tool_idx < TOOLS_COUNT) {
                    ui.tabsSetItemIndex(tool_idx);
                    ESP_LOGI(TAG, "✓ Tools item restored: %d", tool_idx);
                } else {
                    ESP_LOGW(TAG, "✗ Invalid tool index: %d", tool_idx);
                }
            }

            // Xóa state file (one-time restore)
            ui_state_clear();
            ESP_LOGI(TAG, "✓ State file cleared");
        } else {
            ESP_LOGE(TAG, "✗ State load failed: %d", load_ret);
        }
    }

    // Log final state before entering UI loop
    ESP_LOGI(TAG, "=== Starting UI with tab=%d, item=%d ===",
             ui.tabsGetCurrent(), ui.tabsGetItemIndex());

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

    // Load flash history (phải sau sd_load_metadata vì cần g_firmware_map)
    sd_history_load();
    histDisplayItems = sd_history_get_display(histCount);
    histIdItems = sd_history_get_ids();
    ESP_LOGI(TAG, "Flash history: %d entries", histCount);

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

    // Sync netflash label + reconnect WiFi if NetFlash was ON before restart
    if (action_netflash_is_enabled()) {
        snprintf(s_netflash_label, sizeof(s_netflash_label), "7. NetFlash: ON ");
        ui.showMessage("NetFlash", "Connecting WiFi...");
        if (wifi_config_connect()) {
            net_server_start();
        }
    }

    // Splash screen
    ui.showMessage("FlashPorter", "Universal Flasher");
    vTaskDelay(pdMS_TO_TICKS(700));

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
