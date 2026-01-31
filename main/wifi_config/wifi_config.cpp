#include "wifi_config.h"

// --- THƯ VIỆN CẦN THIẾT ---
#include <WiFiManager.h> 
#include <SD.h>
#include <FS.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

// --- MODULE KHÁC TRONG DỰ ÁN ---
#include "../oled/menu.h"
#include "../sd_card/sd_card.h"
#include "../file_utils/file_utils.h"  // Centralized file operations

// --- BIẾN TOÀN CỤC NỘI BỘ (STATIC) ---
static const char *TAG = "WIFI_CFG"; // Tag log cho module này
static bool shouldSaveConfig = false; // Cờ để biết có cần lưu config không

// ============================================================
// 1. KHAI BÁO HÀM NỘI BỘ (INTERNAL PROTOTYPES)
// ============================================================
static void _save_config_callback();
static esp_err_t _read_file(const char* path, char* buffer, size_t size, const char* default_val);
static esp_err_t _write_file(const char* path, const char* value);
static String _normalize_git_url(const char* input_url);  // [NEW] Forward declaration

// ============================================================
// 2. HÀM CÔNG KHAI (PUBLIC FUNCTIONS)
// ============================================================

// Hàm lấy toàn bộ thông tin cấu hình ra ngoài (để module Download sử dụng)
void wifi_config_get_params(char* out_url, char* out_key, char* out_iv) {
    _read_file(CONFIG_FILE_URL, out_url, 256, DEFAULT_URL);
    _read_file(CONFIG_FILE_KEY, out_key, 33, DEFAULT_KEY);
    _read_file(CONFIG_FILE_IV,  out_iv, 33, DEFAULT_IV);
}
bool wifi_config_connect() {
    WiFiManager wm;
    wm.setConfigPortalTimeout(180); 
    oled_show_message("Connecting to", "WiFi...");
    return wm.autoConnect("Universal-Flasher-Config");
}

// Hàm Setup chính: Hiện 3 ô nhập liệu
void wifi_config_force_portal() {
    ESP_LOGI(TAG, ">>> OPEN SETUP PORTAL");
    oled_show_message("Setup Mode", "Connect Wifi...");

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
        oled_show_message("Setup", "Cancelled");
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

            // Lưu xuống 3 file riêng biệt
            _write_file(CONFIG_FILE_URL, normalized_url.c_str());
            _write_file(CONFIG_FILE_KEY, new_key);
            _write_file(CONFIG_FILE_IV,  new_iv);

            oled_show_message("Setup", "Saved & Reboot");
            delay(2000);
            
            esp_restart(); // Reset ngay để áp dụng
        }
    }
    wifi_config_stop();
}

void wifi_config_stop() {
    ESP_LOGI(TAG, ">>> Ngat ket noi Wifi...");
    WiFi.disconnect(true); // Ngắt kết nối và xóa cấu hình
    WiFi.mode(WIFI_OFF);         // Tắt Radio WiFi để tiết kiệm pin
    ESP_LOGI(TAG, "Wifi stopped.");
}

// ============================================================
// 3. HÀM NỘI BỘ (INTERNAL IMPLEMENTATION)
// ============================================================

// Callback báo hiệu người dùng bấm nút Save trên Web
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

// Ghi file text (Tự tạo thư mục /config nếu thiếu)
static esp_err_t _write_file(const char* path, const char* value) {
    // fu_file_write tự động tạo parent dir và xóa file cũ
    esp_err_t err = fu_file_write(path, value);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved: %s -> %s", path, value);
    } else {
        ESP_LOGE(TAG, "Save Failed: %s", path);
    }
    return err;
}

