/**
 * @file main.cpp
 * @brief Điểm khởi động firmware CHIVI FlashPorter — TFT ST7735 + ESP32-S3
 *
 * Chức năng chính:
 *   - Khởi tạo SD card, màn hình TFT ST7735, WiFi và các module flasher khi boot
 *   - Điều phối giao diện dạng tab qua thư viện TftUI:
 *       Tab 0 (FW)    — danh sách firmware, chọn để nạp ngay
 *       Tab 1 (Tools) — đồng bộ git, monitor UART, xóa chip, quét thiết bị
 *       Tab 2 (Desc)  — xem mô tả firmware, chọn để nạp
 *       Tab 3 (Hist)  — lịch sử các lần nạp gần nhất
 *       Tab 4 (Info)  — thông tin phiên bản, tác giả
 *   - Lưu và khôi phục trạng thái giao diện (tab + item đang chọn) qua ui_state
 *   - Định nghĩa callback cho từng hành động người dùng trên UI
 */

#include "Arduino.h"
#include <SPI.h>

// ============================================================
// 1. ESP-IDF & SYSTEM HEADERS
// ============================================================
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================
// 2. PROJECT MODULES
// ============================================================
#include "pin_config.h"
#include "firmware_types.h"
#include "usb_drive/usb_drive.h"       // USB MSC + CDC (replaces SD card)
#include "sd_card/sd_card.h"
#include "flasher/flasher_common.h"
#include "tft_ui.h"
#include "app_actions/app_actions.h"
#include "ui_state/ui_state.h"
#include "flash_log/flash_log.h"

// ============================================================
// 3. CONSTANTS & GLOBALS
// ============================================================
static const char *TAG = "MAIN_APP";

// TFT Display (hardware SPI — 3-arg constructor)
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

// TftUI Instance
TftUI ui;

// ============================================================
// TAB CONFIGURATION
// ============================================================
#define TAB_FW      0
#define TAB_TOOLS   1
// #define TAB_DESC    2  // Desc tab disabled
// #define TAB_HIST    3  // History tab disabled
#define TAB_INFO    2

const char* tabNames[] = {"FW", "Tools", /* "Desc", "Hist", */ "Info"};
const int TAB_COUNT = 3;

// Firmware items (loaded from SD)
const char** fwDisplayItems = nullptr;
const char** fwIdItems = nullptr;
int fwCount = 0;

// // History items (loaded from SD after metadata)
// const char** histDisplayItems = nullptr;
// const char** histIdItems = nullptr;
// int histCount = 0;

// UI state (pre-loaded before USB unlock)
static int  s_saved_tab = -1;
static char s_saved_item[64] = {0};

// Tools items (offline mode — no WiFi/Sync)
const char* toolsItems[] = {
    "1. Erase Chip (ESP)",
    "2. Erase Chip (SWD)",
    "3. Scan Boot",
    "4. Monitor/Test",
    "5. Re-scan USB",
};
const int TOOLS_COUNT = 5;

// Tab item counts
int tabItemCounts[3] = {0, 5, 0};  // FW:dynamic, Tools:5, Info:0

// ============================================================
// UI CALLBACKS CHO APP_ACTIONS
// ============================================================

// Callback ve man hinh monitor — dung TFT truc tiep voi mau sac
static void ui_monitor_draw_cb(char log_buffer[APP_ACTIONS_LOG_LINES][APP_ACTIONS_LOG_LINE_WIDTH + 1],
                                int log_write_index, bool has_activity) {
    Adafruit_ST7735* d = ui.getDisplay();
    d->fillScreen(UI_COL_BG);
    d->setTextSize(1);

    // Header bar
    d->fillRect(0, 0, UI_SCREEN_W, UI_HEADER_H, UI_COL_HEADER_BG);
    d->setTextColor(UI_COL_HEADER_FG);
    d->setCursor(4, 3);
    if (has_activity) {
        d->print("MONITOR");
        d->setTextColor(ST77XX_GREEN);
        d->setCursor(UI_SCREEN_W - 30, 3);
        d->print("[RX]");
    } else {
        d->print("MONITOR [OK]=Exit");
    }

    // Log lines — TFT 160x128 landscape, ~9 lines
    d->setTextColor(ST77XX_GREEN);
    int yPos = UI_HEADER_H + 2;
    int maxLines = (UI_SCREEN_H - UI_HEADER_H - 4) / 10;
    if (maxLines > APP_ACTIONS_LOG_LINES - 1) maxLines = APP_ACTIONS_LOG_LINES - 1;

    for (int i = 0; i < maxLines; i++) {
        int idx = (log_write_index + i) % APP_ACTIONS_LOG_LINES;
        d->setCursor(0, yPos);
        d->println(log_buffer[idx]);
        yPos += 10;
    }
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
    // Landscape 160x128: header 14px → ~8 items visible
    int maxVisible = (ui.tabsGetHeaderHeight() > 0) ? 8 : 10;

    switch (tab) {
        case TAB_FW:  // Firmware list - OK to flash
            if (fwCount > 0 && fwDisplayItems) {
                ui.drawList(fwDisplayItems, fwCount, itemIdx, yStart, maxVisible);
            } else {
                ui.drawText("No Firmware", 4, yStart);
                ui.drawText("Please Sync first", 4, yStart + 12);
            }
            break;

        case TAB_TOOLS:
            ui.drawList(toolsItems, TOOLS_COUNT, itemIdx, yStart, maxVisible);
            break;

        // case TAB_DESC: { // Description tab (disabled)
        //     ...
        // }

        // case TAB_HIST:  // Flash history (disabled)
        //     if (histCount > 0 && histDisplayItems) {
        //         ui.drawList(histDisplayItems, histCount, itemIdx, yStart, maxVisible);
        //     } else {
        //         ui.drawText("No history yet", 4, yStart);
        //         ui.drawText("Flash to start", 4, yStart + 12);
        //     }
        //     break;

        case TAB_INFO:  // Info
            ui.drawKeyValue("Version", "1.1.0", yStart);
            ui.drawKeyValue("Author", "TTP27", yStart + 14);
            ui.drawKeyValue("Build", "260301", yStart + 28);
            ui.drawKeyValue("Chip", "ESP32-S3", yStart + 42);
            ui.drawKeyValue("Display", "ST7735", yStart + 56);
            ui.drawKeyValue("Screen", "160x128", yStart + 70);
            break;
    }
}

// ============================================================
// HANDLE TAB SELECTION
// ============================================================
void handleTabSelection(int tab, int itemIdx) {
    auto saveAndRestart = [&](const char* item_id) {
        host_system_restart(tab, item_id);
    };

    switch (tab) {
        case TAB_FW:
            if (fwCount > 0 && fwIdItems && itemIdx < fwCount) {
                const char* fw_id = fwIdItems[itemIdx];
                esp_err_t ret = action_flash_firmware(fw_id);
                if (ret == ESP_OK) {
                    usb_drive_lock();  // Re-lock VFS cho state save
                    // sd_history_add(fw_id);  // History disabled
                    saveAndRestart(fw_id);  // → esp_restart(), no unlock needed
                }
            }
            break;

        case TAB_TOOLS:
            switch (itemIdx) {
                case 0: {  // Erase Chip (ESP)
                    esp_err_t ret = action_erase_chip();
                    if (ret == ESP_OK) {
                        usb_drive_lock();
                        saveAndRestart("0");
                    }
                    break;
                }
                case 1: {  // Erase Chip (SWD)
                    esp_err_t ret = action_erase_chip_swd();
                    if (ret == ESP_OK) {
                        usb_drive_lock();
                        saveAndRestart("1");
                    }
                    break;
                }
                case 2:  // Scan Boot
                    action_scan_boot();
                    usb_drive_lock();
                    saveAndRestart("2");
                    break;
                case 3:  // Monitor/Test
                    action_monitor();
                    break;  // Return to tabs — no restart needed
                case 4: {  // Re-scan USB
                    ui.showMessage("Re-scan", "Switching to APP...");
                    usb_drive_lock();
                    vTaskDelay(pdMS_TO_TICKS(200));

                    ui.showMessage("Re-scan", "Reading USB drive...");
                    sd_load_metadata();
                    fwDisplayItems = sd_get_menu_display_items(fwCount);
                    fwIdItems = sd_get_menu_id_items();

                    // sd_history_load();  // History disabled
                    // histDisplayItems = sd_history_get_display(histCount);
                    // histIdItems = sd_history_get_ids();

                    usb_drive_unlock();

                    char msg[32];
                    snprintf(msg, sizeof(msg), "Found %d firmware", fwCount);
                    ui.showMessage("Re-scan Done", msg);
                    vTaskDelay(pdMS_TO_TICKS(1500));

                    // Update tab item counts
                    tabItemCounts[TAB_FW]   = fwCount;
                    // tabItemCounts[TAB_DESC] = fwCount;  // Desc tab disabled
                    // tabItemCounts[TAB_HIST] = histCount;  // History disabled
                    break;
                }
            }
            break;

        // case TAB_DESC:  // Desc tab disabled
        //     if (fwCount > 0 && fwIdItems && itemIdx < fwCount) {
        //         const char* fw_id = fwIdItems[itemIdx];
        //         esp_err_t ret = action_flash_firmware(fw_id);
        //         if (ret == ESP_OK) {
        //             usb_drive_lock();
        //             saveAndRestart(fw_id);
        //         }
        //     }
        //     break;

        // case TAB_HIST:  // History tab disabled
        //     if (histCount > 0 && histIdItems && itemIdx < histCount) {
        //         const char* fw_id = histIdItems[itemIdx];
        //         esp_err_t ret = action_flash_firmware(fw_id);
        //         if (ret == ESP_OK) {
        //             usb_drive_lock();
        //             sd_history_add(fw_id);
        //             saveAndRestart(fw_id);
        //         }
        //     }
        //     break;

        case TAB_INFO:
            break;
    }
}

// ============================================================
// MAIN TABS LOOP
// ============================================================
void runTabsUI() {
    tabItemCounts[TAB_FW]    = fwCount;
    tabItemCounts[TAB_TOOLS] = TOOLS_COUNT;
    // tabItemCounts[TAB_DESC]  = fwCount;  // Desc tab disabled
    // tabItemCounts[TAB_HIST]  = histCount;  // History tab disabled
    tabItemCounts[TAB_INFO]  = 0;

    ui.tabsCreate(tabNames, TAB_COUNT, tabItemCounts);
    ui.tabsSetHeaderStyle(UI_HEADER_SINGLE);
    ui.tabsSetHeaderMode(UI_HEADER_ALWAYS);
    ui.tabsSetDrawCallback(drawTabContent);

    // --- RESTORE UI STATE (pre-loaded in setup, no file access needed) ---
    if (s_saved_tab >= 0) {
        ESP_LOGI(TAG, "Restoring UI state: tab=%d, item=%s", s_saved_tab, s_saved_item);
        ui.tabsSetCurrent(s_saved_tab);

        if (s_saved_tab == TAB_FW) {
            for (int i = 0; i < fwCount; i++) {
                if (fwIdItems && strcmp(fwIdItems[i], s_saved_item) == 0) {
                    ui.tabsSetItemIndex(i);
                    break;
                }
            }
        // } else if (s_saved_tab == TAB_DESC) {  // Desc tab disabled
        // } else if (s_saved_tab == TAB_HIST) {  // History tab disabled
        //     for (int i = 0; i < histCount; i++) {
        //         if (histIdItems && strcmp(histIdItems[i], s_saved_item) == 0) {
        //             ui.tabsSetItemIndex(i);
        //             break;
        //         }
        //     }
        } else if (s_saved_tab == TAB_TOOLS) {
            int tool_idx = atoi(s_saved_item);
            if (tool_idx >= 0 && tool_idx < TOOLS_COUNT) {
                ui.tabsSetItemIndex(tool_idx);
            }
        }
        s_saved_tab = -1;  // Used once, reset
    } else {
        ESP_LOGI(TAG, "No saved state - starting fresh");
    }

    ESP_LOGI(TAG, "=== Starting UI with tab=%d, item=%d ===",
             ui.tabsGetCurrent(), ui.tabsGetItemIndex());

    ui.tabsRun();

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
    ESP_LOGI(TAG, "--- SYSTEM START (CHIVI-TFT, ESP32-S3) ---");

    // Init hardware SPI bus voi custom pins
    SPI.begin(TFT_SCLK, -1, TFT_MOSI);  // SCK, MISO (ko dung), MOSI

    // Init TFT (hardware SPI — nhanh)
    tft.setSPISpeed(40000000);      // 40MHz
    tft.initR(INITR_BLACKTAB);     // hoac INITR_GREENTAB tuy module
    tft.setRotation(3);             // Landscape 160x128 (flipped)
    tft.fillScreen(ST77XX_BLACK);

    // Init TftUI (them TFT_BL cho backlight)
    ui.begin(&tft, BTN_UP, BTN_DOWN, BTN_OK, TFT_BL);
    ui.showMessage("Booting...", "Init Hardware");

    // Init USB drive (mount FAT partition + bật TinyUSB MSC/CDC)
    // auto_mount_off=1: ESP32 giữ quyền đọc FAT (APP mode) cho đến khi chủ động unlock
    ui.showMessage("Booting...", "Init USB Drive");
    if (usb_drive_init() != ESP_OK) {
        ui.showMessage("Error", "USB Drive Init Fail");
        for(;;);
    }

    // Xác nhận firmware store sẵn sàng (drive đã mount ở APP mode)
    if (sd_mount(-1) != ESP_OK) {
        ui.showMessage("Error", "FW Store Not Ready");
        for(;;);
    }

    // Scan firmware folders (ESP32 đang ở APP mode → VFS active → opendir works)
    sd_load_metadata();
    fwDisplayItems = sd_get_menu_display_items(fwCount);
    fwIdItems = sd_get_menu_id_items();

    // Nếu không tìm thấy FW → cho user cắm USB copy, nhấn OK để re-scan
    while (fwCount == 0) {
        ESP_LOGW(TAG, "No firmware found — waiting for USB copy");
        // Chuyển USB mode → PC thấy ổ đĩa
        usb_drive_unlock();

        Adafruit_ST7735* d = ui.getDisplay();
        d->fillScreen(ST77XX_BLACK);
        d->setTextSize(1);
        d->setTextColor(ST77XX_YELLOW);
        d->setCursor(4, 4);  d->print("No Firmware Found");
        d->setTextColor(ST77XX_WHITE);
        d->setCursor(4, 20); d->print("Copy FW to USB drive:");
        d->setCursor(4, 34); d->print("  ST_<name>/FW.bin");
        d->setCursor(4, 46); d->print("  ES_<name>/app.bin");
        d->setTextColor(ST77XX_GREEN);
        d->setCursor(4, 66); d->print("[OK] Re-scan");
        d->setTextColor(UI_COL_HINT);
        d->setCursor(4, 80); d->print("Eject drive on PC");
        d->setCursor(4, 92); d->print("before pressing OK");

        // Chờ nhấn OK
        while (digitalRead(BTN_OK) == HIGH) vTaskDelay(pdMS_TO_TICKS(50));
        while (digitalRead(BTN_OK) == LOW)  vTaskDelay(pdMS_TO_TICKS(50));

        // Chuyển lại APP mode → ESP32 đọc FAT
        usb_drive_lock();
        ui.showMessage("Scanning...", "Reading USB drive");
        vTaskDelay(pdMS_TO_TICKS(200));

        sd_load_metadata();
        fwDisplayItems = sd_get_menu_display_items(fwCount);
        fwIdItems = sd_get_menu_id_items();
    }

    // // Load flash history (disabled — History tab commented out)
    // sd_history_load();
    // histDisplayItems = sd_history_get_display(histCount);
    // histIdItems = sd_history_get_ids();
    // ESP_LOGI(TAG, "Flash history: %d entries", histCount);

    // Load + clear UI state (cần VFS → làm trước unlock)
    if (ui_state_exists()) {
        ui_state_load(&s_saved_tab, s_saved_item, sizeof(s_saved_item));
        ui_state_clear();
        ESP_LOGI(TAG, "UI state pre-loaded: tab=%d, item=%s", s_saved_tab, s_saved_item);
    }

    // Auto-unlock USB → PC thấy ổ đĩa + monitor hoạt động
    usb_drive_unlock();

    // Init App Actions
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

    // Splash screen — TFT voi mau sac
    Adafruit_ST7735* d = ui.getDisplay();
    d->fillScreen(ST77XX_BLACK);
    d->setTextSize(2);
    d->setTextColor(ST77XX_CYAN);
    int tw = 11 * 12;  // "FlashPorter" = 11 chars * 12px
    d->setCursor((UI_SCREEN_W - tw) / 2, 25);
    d->print("FlashPorter");

    d->setTextSize(1);
    d->setTextColor(ST77XX_WHITE);
    const char* sub = "CHIVI Universal Flasher";
    int sw = strlen(sub) * 6;
    d->setCursor((UI_SCREEN_W - sw) / 2, 55);
    d->print(sub);

    d->setTextColor(UI_COL_HINT);
    const char* ver = "v2.0 - Offline USB";
    int vw = strlen(ver) * 6;
    d->setCursor((UI_SCREEN_W - vw) / 2, 75);
    d->print(ver);

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
