/**
 * @file ble_provision.cpp
 * @brief WiFi provisioning qua BLE bằng arduino-esp32 WiFiProv (scheme BLE).
 *
 * Luồng:
 *   1. Đăng ký event handler cho các sự kiện PROV_* của arduino.
 *   2. WiFiProv.beginProvision(BLE) → dựng BLE GATT service tên "PROV_xxxx".
 *   3. App "ESP BLE Provisioning" kết nối, gửi SSID/password.
 *   4. PROV_CRED_RECV  → mạch thử kết nối AP.
 *      PROV_CRED_SUCCESS → creds đã được esp_wifi lưu NVS → reboot.
 *      PROV_CRED_FAIL    → sai pass / không thấy AP → báo, chờ app gửi lại.
 *
 * BLE chỉ chạy trong lúc provisioning; FREE_BTDM giải phóng bộ nhớ controller
 * sau khi xong, và ta reboot về chế độ WiFi-only bình thường.
 */

#include "ble_provision.h"

#include <Arduino.h>
#include "WiFi.h"
#include "WiFiProv.h"

#include "esp_mac.h"
#include "esp_system.h"
#include "esp_log.h"
#include "network_provisioning/manager.h"   // network_prov_mgr_deinit() khi hủy

#include "oled_ui.h"

// OLED UI toàn cục (khai báo ở main.cpp) — dùng chung như các module khác.
extern OledUI ui;

static const char *TAG = "BLE_PROV";

// PIN (proof-of-possession) người dùng nhập trong app. Cố định cho tiện.
#define BLE_PROV_POP        "flash123"
// Timeout tổng khi chờ điện thoại (ms).
#define BLE_PROV_TIMEOUT_MS 180000UL

// ── Trạng thái provisioning (đặt từ event task) ─────────────────────────────
enum { PS_WAIT = 0, PS_RECV = 1, PS_SUCCESS = 2, PS_FAIL = 3 };
static volatile int  s_prov_state = PS_WAIT;
static char          s_prov_ssid[33] = {0};
static bool          s_evt_registered = false;

// CẢNH BÁO: chạy trong FreeRTOS task riêng của WiFi — chỉ set cờ, không vẽ OLED.
static void _prov_event(arduino_event_t *e)
{
    switch (e->event_id) {
        case ARDUINO_EVENT_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;
        // Lưới an toàn: có IP nghĩa là đã join WiFi thành công, kể cả khi
        // event PROV_CRED_SUCCESS không bắn (app ngắt BLE sớm). Creds lúc này
        // đã được esp_wifi lưu NVS nên reboot xong vẫn tự kết nối lại.
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            s_prov_state = PS_SUCCESS;
            ESP_LOGI(TAG, "STA got IP → treat as success");
            break;
        // Log lý do rớt để soi trên monitor (sai pass / không thấy AP…).
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            ESP_LOGW(TAG, "STA disconnected (reason=%d)",
                     e->event_info.wifi_sta_disconnected.reason);
            break;
        case ARDUINO_EVENT_PROV_CRED_RECV: {
            const char *ssid = (const char *)e->event_info.prov_cred_recv.ssid;
            const char *pwd  = (const char *)e->event_info.prov_cred_recv.password;
            strncpy(s_prov_ssid, ssid, sizeof(s_prov_ssid) - 1);
            s_prov_ssid[sizeof(s_prov_ssid) - 1] = '\0';
            s_prov_state = PS_RECV;
            ESP_LOGI(TAG, "===> CRED_RECV  SSID='%s' (len=%d)  PASS_len=%d",
                     s_prov_ssid, (int)strlen(ssid), (int)strlen(pwd));
            break;
        }
        case ARDUINO_EVENT_PROV_CRED_FAIL: {
            int reason = (int)e->event_info.prov_fail_reason;
            s_prov_state = PS_FAIL;
            // 0 = WIFI_STA_AUTH_ERROR (sai pass), 1 = WIFI_STA_AP_NOT_FOUND
            ESP_LOGW(TAG, "===> CRED_FAIL  reason=%d (%s)", reason,
                     reason == 0 ? "AUTH_ERROR/sai-pass" : "AP_NOT_FOUND/khong-thay-mang");
            break;
        }
        case ARDUINO_EVENT_PROV_CRED_SUCCESS:
            s_prov_state = PS_SUCCESS;
            ESP_LOGI(TAG, "===> CRED_SUCCESS");
            break;
        case ARDUINO_EVENT_PROV_END:
            ESP_LOGI(TAG, "===> PROV_END");
            break;
        default:
            ESP_LOGI(TAG, "evt id=%d", (int)e->event_id);
            break;
    }
}

bool ble_provision_run(uint8_t btn_ok_pin)
{
    s_prov_state = PS_WAIT;
    s_prov_ssid[0] = '\0';

    // Bật log chi tiết cho toàn bộ chuỗi provisioning + WiFi để soi trên monitor.
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("network_prov_mgr", ESP_LOG_DEBUG);
    esp_log_level_set("wifi_prov_mgr",    ESP_LOG_DEBUG);
    esp_log_level_set("protocomm",        ESP_LOG_DEBUG);
    esp_log_level_set("protocomm_ble",    ESP_LOG_DEBUG);
    esp_log_level_set("wifi",             ESP_LOG_INFO);
    ESP_LOGI(TAG, "==================== BLE PROVISION START ====================");

    // Tên thiết bị BLE = "PROV_" + 2 byte cuối MAC (đọc từ efuse, không cần start WiFi).
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    static char service_name[16];
    snprintf(service_name, sizeof(service_name), "PROV_FL%02X%02X", mac[4], mac[5]);

    // Đăng ký 1 lần (WiFi.onEvent cộng dồn handler nếu gọi lại).
    if (!s_evt_registered) {
        WiFi.onEvent(_prov_event);
        s_evt_registered = true;
    }

    ESP_LOGI(TAG, "Begin BLE provisioning: name=%s pop=%s", service_name, BLE_PROV_POP);

    // reset_provisioned=true → cho phép nạp lại creds mỗi lần vào menu.
    // FREE_BTDM → giải phóng RAM controller sau khi provisioning kết thúc.
    uint8_t uuid[16] = {
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02
    };
    WiFiProv.beginProvision(
        NETWORK_PROV_SCHEME_BLE,
        NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM,
        NETWORK_PROV_SECURITY_1,
        BLE_PROV_POP,
        service_name,
        NULL,        // service_key (chỉ dùng cho SoftAP)
        uuid,
        true);       // reset_provisioned

    // Màn hình hướng dẫn: tên thiết bị + PIN.
    ui.showMessage("Mo app dien thoai", "ESP BLE Provision");
    delay(2200);
    {
        char pin_line[24];
        snprintf(pin_line, sizeof(pin_line), "PIN: %s", BLE_PROV_POP);
        ui.showMessage(service_name, pin_line);
    }
    delay(2800);

    // Bỏ qua lần nhả nút OK vừa dùng để chọn menu, tránh hủy ngay lập tức.
    ui.waitRelease();

    // ── Vòng chờ ────────────────────────────────────────────────────────────
    uint32_t start      = millis();
    uint32_t last_dbg   = 0;
    uint32_t recv_start = 0;   // mốc lúc nhận creds, để phát hiện join lâu quá
    int      frame      = 0;
    bool     received   = false;

    while ((millis() - start) < BLE_PROV_TIMEOUT_MS) {
        int st = s_prov_state;

        // Nhịp tim ~1.5s: in trạng thái + WiFi.status() ra monitor.
        if (millis() - last_dbg > 1500) {
            last_dbg = millis();
            ESP_LOGI(TAG, "[hb] prov_state=%d  wifi_status=%d  ssid='%s'  elapsed=%lus",
                     st, (int)WiFi.status(), s_prov_ssid,
                     (unsigned long)((millis() - start) / 1000));
        }

        if (st == PS_SUCCESS) {
            received = true;
            ui.showMessage("WiFi OK!", "Luu & khoi dong");
            delay(1600);
            esp_restart();          // creds đã lưu NVS → reboot vào WiFi-only
        } else if (st == PS_FAIL) {
            ui.showMessage("Sai mat khau", "Thu lai tren app");
            delay(1800);
            s_prov_state = PS_WAIT; // chờ app gửi lại
            start = millis();       // gia hạn timeout
        } else if (st == PS_RECV) {
            if (recv_start == 0) recv_start = millis();
            // Join > 20s mà chưa xong → gần như chắc chọn nhầm mạng 5GHz
            // (C3 chỉ 2.4GHz) hoặc sai pass. Cảnh báo nhưng vẫn chờ app gửi lại.
            if (millis() - recv_start > 20000) {
                ui.showMessage("Ko vao duoc mang", "Chon WiFi 2.4GHz!");
            } else {
                ui.showMessage("Da nhan WiFi", "Dang ket noi...");
            }
            delay(300);
        } else {
            ui.showSpinner("Cho dt gui WiFi", frame++ & 3);
        }

        // Bấm OK để hủy giữa chừng.
        if (ui.btnPressed(btn_ok_pin)) {
            ESP_LOGI(TAG, "User cancelled BLE provisioning");
            break;
        }
        delay(250);
    }

    // Dọn dẹp nếu không thành công (thành công đã esp_restart ở trên).
    // endProvision() chỉ stop; deinit tường minh để lần sau vào lại init được.
    WiFiProv.endProvision();
    network_prov_mgr_deinit();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    if (!received) {
        ui.showMessage("BLE Setup", "Da thoat");
        delay(800);
    }
    return received;
}
