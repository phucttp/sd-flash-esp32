/**
 * @file wifi_config.cpp
 * @brief Cấu hình WiFi và quản lý tập trung các tham số kết nối — URL git, AES key/IV.
 *
 * Chức năng chính:
 *   - Khởi động WiFiManager để cấu hình SSID/password qua captive portal nếu chưa có
 *   - Đọc URL git repository từ file cấu hình trên SD card (/git_url.txt)
 *   - Lưu và đọc AES key + IV từ NVS (ESP32 internal flash) — không lưu trên SD card
 *   - Cung cấp hàm getter tập trung cho các tham số: URL, key, IV
 */

#include "wifi_config.h"

// ============================================================
// INCLUDES
// ============================================================
#include <WiFiManager.h>
#include <SD.h>
#include <FS.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "oled_ui.h"
#include "../sd_card/sd_card.h"
#include "../file_utils/file_utils.h"

extern OledUI ui;

// ============================================================
// BIẾN TOÀN CỤC
// ============================================================
static const char *TAG = "WIFI_CFG";
static bool shouldSaveConfig = false;

// NVS namespace (Key/IV lưu trong ESP32 flash, KHÔNG phải SD card)
#define NVS_NAMESPACE "flasher_cfg"
#define NVS_KEY_AES   "aes_key"
#define NVS_KEY_IV    "aes_iv"

// ============================================================
// KHAI BÁO HÀM NỘI BỘ
// ============================================================
static void _save_config_callback();
static esp_err_t _read_file(const char* path, char* buffer, size_t size, const char* default_val);
static esp_err_t _write_file(const char* path, const char* value);
static String _normalize_git_url(const char* input_url);

// NVS helpers
static esp_err_t _nvs_read_string(const char* key, char* out, size_t max_len, const char* default_val);
static esp_err_t _nvs_write_string(const char* key, const char* value);

// ============================================================
// HÀM CÔNG KHAI
// ============================================================

/**
 * @brief Lấy thông tin cấu hình (URL từ SD, Key/IV từ NVS)
 */
void wifi_config_get_params(char* out_url, char* out_key, char* out_iv) {
    // URL vẫn lưu trên SD (không nhạy cảm)
    _read_file(CONFIG_FILE_URL, out_url, 256, DEFAULT_URL);

    // Key/IV lưu trong NVS (bảo mật)
    _nvs_read_string(NVS_KEY_AES, out_key, 33, DEFAULT_KEY);
    _nvs_read_string(NVS_KEY_IV,  out_iv,  33, DEFAULT_IV);
}

/**
 * @brief Kết nối WiFi (auto-connect hoặc portal)
 */
bool wifi_config_connect() {
    WiFiManager wm;
    wm.setConfigPortalTimeout(180); 
    ui.showMessage("Connecting to", "WiFi...");
    return wm.autoConnect("Universal-Flasher-Config");
}

/**
 * @brief Mở portal cấu hình (URL, AES Key, IV)
 */
void wifi_config_force_portal() {
    ESP_LOGI(TAG, ">>> OPEN SETUP PORTAL");
    ui.showMessage("Setup Mode", "Connect Wifi...");

    WiFiManager wm;
    shouldSaveConfig = false;
    wm.setSaveConfigCallback(_save_config_callback);
    wm.setBreakAfterConfig(true); // Quan trọng: Bấm Save là thoát

    // --- B1: Chuẩn bị buffer và load dữ liệu cũ ---
    char buf_url[256];
    char buf_key[33]; // Key 16 bytes (hex string có thể dài hơn) hoặc raw char
    char buf_iv[33];

    wifi_config_get_params(buf_url, buf_key, buf_iv);

    // --- B2: Tạo 3 ô nhập liệu ---
    // Ô 1: URL (Hỗ trợ auto-convert GitHub/GitLab/Gitee/Bitbucket)
    WiFiManagerParameter p_url("url", "Git URL (auto-convert to raw)", buf_url, 256);
    wm.addParameter(&p_url);

    // Ô 2: AES Key (Gợi ý: 16 ký tự)
    // Type="text" placeholder="16 chars"
    WiFiManagerParameter p_key("key", "AES Key (16 chars)", buf_key, 32);
    wm.addParameter(&p_key);

    // Ô 3: AES IV (Gợi ý: 16 ký tự)
    WiFiManagerParameter p_iv("iv", "AES IV (16 chars)", buf_iv, 32);
    wm.addParameter(&p_iv);

    // --- B3: Mở Portal ---
    wm.setCaptivePortalEnable(true);
    if (!wm.startConfigPortal("Universal-Flasher-Setup")) {
        ESP_LOGW(TAG, "Timeout/Exit");
        ui.showMessage("Setup", "Cancelled");
        delay(1000);
    } else {
        // --- B4: Xử lý Save ---
        if (shouldSaveConfig) {
            ESP_LOGI(TAG, "Saving new config...");

            // Lấy giá trị từ các ô nhập
            const char* new_url = p_url.getValue();
            const char* new_key = p_key.getValue();
            const char* new_iv  = p_iv.getValue();

            // [FIX] Tự động chuyển đổi URL Git sang Raw URL
            String normalized_url = _normalize_git_url(new_url);
            ESP_LOGI(TAG, "Original URL: %s", new_url);
            ESP_LOGI(TAG, "Normalized URL: %s", normalized_url.c_str());

            // [SECURITY] URL lưu trên SD, Key/IV lưu trong NVS (ESP32 flash)
            _write_file(CONFIG_FILE_URL, normalized_url.c_str());  // SD card (public)
            _nvs_write_string(NVS_KEY_AES, new_key);               // NVS (secure)
            _nvs_write_string(NVS_KEY_IV,  new_iv);                // NVS (secure)

            ESP_LOGI(TAG, "Key/IV saved to NVS (not SD card)");

            ui.showMessage("Setup", "Saved & Reboot");
            delay(2000);
            
            esp_restart(); // Reset ngay để áp dụng
        }
    }
    wifi_config_stop();
}

/**
 * @brief Ngắt kết nối WiFi và tắt radio
 */
void wifi_config_stop() {
    ESP_LOGI(TAG, ">>> Ngat ket noi Wifi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    ESP_LOGI(TAG, "Wifi stopped.");
}

// ============================================================
// HÀM NỘI BỘ
// ============================================================

// Callback khi user bấm Save trên web portal
static void _save_config_callback() {
    ESP_LOGI(TAG, "Callback: User da thay doi cau hinh!");
    shouldSaveConfig = true;
}

/**
 * @brief Tự động chuyển đổi URL Git sang Raw URL
 * Hỗ trợ: GitHub, GitLab, Gitee, Bitbucket
 *
 * Ví dụ:
 * - github.com/.../blob/main/... → raw.githubusercontent.com/.../main/...
 * - gitlab.com/.../-/blob/...    → gitlab.com/.../-/raw/...
 * - gitee.com/.../blob/...       → gitee.com/.../raw/...
 * - bitbucket.org/.../src/...    → bitbucket.org/.../raw/...
 */
static String _normalize_git_url(const char* input_url) {
    String url(input_url);
    url.trim();

    // --- GitHub ---
    // https://github.com/user/repo/blob/branch/path/file
    // → https://raw.githubusercontent.com/user/repo/branch/path/file
    if (url.indexOf("github.com") >= 0 && url.indexOf("/blob/") >= 0) {
        url.replace("github.com", "raw.githubusercontent.com");
        url.replace("/blob/", "/");
        ESP_LOGI(TAG, "Converted GitHub URL");
    }
    // --- GitLab ---
    // https://gitlab.com/user/repo/-/blob/branch/path/file
    // → https://gitlab.com/user/repo/-/raw/branch/path/file
    else if (url.indexOf("gitlab.com") >= 0 && url.indexOf("/-/blob/") >= 0) {
        url.replace("/-/blob/", "/-/raw/");
        ESP_LOGI(TAG, "Converted GitLab URL");
    }
    // --- Gitee (Chinese GitHub) ---
    // https://gitee.com/user/repo/blob/branch/path/file
    // → https://gitee.com/user/repo/raw/branch/path/file
    else if (url.indexOf("gitee.com") >= 0 && url.indexOf("/blob/") >= 0) {
        url.replace("/blob/", "/raw/");
        ESP_LOGI(TAG, "Converted Gitee URL");
    }
    // --- Bitbucket ---
    // https://bitbucket.org/user/repo/src/branch/path/file
    // → https://bitbucket.org/user/repo/raw/branch/path/file
    else if (url.indexOf("bitbucket.org") >= 0 && url.indexOf("/src/") >= 0) {
        url.replace("/src/", "/raw/");
        ESP_LOGI(TAG, "Converted Bitbucket URL");
    }
    // Nếu đã là raw URL hoặc URL lạ → giữ nguyên
    else {
        ESP_LOGD(TAG, "URL unchanged (already raw or unknown format)");
    }

    return url;
}

// Đọc file text. Nếu file không có HOẶC file rỗng -> Dùng Default
static esp_err_t _read_file(const char* path, char* buffer, size_t size, const char* default_val) {
    // Dùng file_utils để đọc với fallback tự động
    return fu_file_read_buf(path, buffer, size, default_val);
}

// Ghi file text
static esp_err_t _write_file(const char* path, const char* value) {
    esp_err_t err = fu_file_write(path, value);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved: %s -> %s", path, value);
    } else {
        ESP_LOGE(TAG, "Save Failed: %s", path);
    }
    return err;
}

// ============================================================
// NVS HELPERS - Lưu Key/IV an toàn trong flash ESP32
// ============================================================

/**
 * @brief Đọc chuỗi từ NVS, fallback về default nếu không có
 */
static esp_err_t _nvs_read_string(const char* key, char* out, size_t max_len, const char* default_val) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);

    if (err != ESP_OK) {
        // NVS namespace doesn't exist yet, use default
        ESP_LOGW(TAG, "NVS namespace not found, using default for '%s'", key);
        strncpy(out, default_val, max_len - 1);
        out[max_len - 1] = '\0';
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t required_size = max_len;
    err = nvs_get_str(handle, key, out, &required_size);

    if (err != ESP_OK) {
        // Key not found, use default
        ESP_LOGW(TAG, "NVS key '%s' not found, using default", key);
        strncpy(out, default_val, max_len - 1);
        out[max_len - 1] = '\0';
    } else {
        ESP_LOGI(TAG, "NVS loaded '%s' (%d chars)", key, strlen(out));
    }

    nvs_close(handle);
    return err;
}

/**
 * @brief Ghi chuỗi vào NVS
 */
static esp_err_t _nvs_write_string(const char* key, const char* value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write NVS key '%s': %s", key, esp_err_to_name(err));
    } else {
        err = nvs_commit(handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "NVS saved '%s' (secure storage)", key);
        }
    }

    nvs_close(handle);
    return err;
}

