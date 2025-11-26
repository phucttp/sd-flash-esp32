#include "sync_engine.h"

// --- 1. INCLUDE CÁC MODULE VỆ TINH ---
#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "esp_log.h"

#include "../firmware_types.h"       
#include "../wifi_config/wifi_config.h"     
#include "../ota_downloader/ota_downloader.h" 
#include "../metadata_parser/metadata_parser.h" 
#include "../sd_card/sd_card.h"             
#include "../oled/menu.h"                   

static const char* TAG = "SYNC_ENGINE";

// ============================================================
// ĐỊNH NGHĨA TÊN FILE
// ============================================================
// File tạm để tải về (Tên phải ngắn < 8 ký tự để tránh lỗi FAT32)
#define FILE_IDX_TMP   "/idx_new.txt" 

// ============================================================
// HÀM PHỤ TRỢ
// ============================================================

// Helper: Tạo URL tải về
String build_download_url(String baseUrl, String relPath) {
    int lastSlash = baseUrl.lastIndexOf('/');
    String rootUrl = (lastSlash > 0) ? baseUrl.substring(0, lastSlash + 1) : baseUrl + "/";
    return rootUrl + relPath;
}

// Helper: Cập nhật Index (Xóa cũ -> Đổi tên Mới -> Chính)
// Helper: Cập nhật Index (Đọc file tạm -> Sửa đuôi .enc thành .bin -> Ghi file chính)
// Helper: Cập nhật Index (Đọc file tạm -> Sửa đuôi .enc thành .bin -> THÊM DẤU / -> Ghi file chính)
bool update_index_file() {
    ESP_LOGI(TAG, "Converting index paths (.enc -> .bin) & Adding '/'...");

    // 1. Mở file tạm
    File fileTmp = SD.open(FILE_IDX_TMP, FILE_READ);
    if (!fileTmp) {
        ESP_LOGE(TAG, "Failed to open temp index: %s", FILE_IDX_TMP);
        return false;
    }

    // 2. Parse JSON
    DynamicJsonDocument doc(20480);
    DeserializationError error = deserializeJson(doc, fileTmp);
    fileTmp.close();

    if (error) {
        ESP_LOGE(TAG, "JSON Parse Error: %s", error.c_str());
        SD.remove(FILE_IDX_TMP);
        return false;
    }

    // 3. Duyệt mảng và sửa đổi đường dẫn
    JsonArray root = doc.as<JsonArray>();
    for (JsonObject obj : root) {
        
        // Lambda sửa đường dẫn: Đổi đuôi & Thêm dấu /
        auto fix_path = [](JsonObject& o, const char* key) {
            if (o.containsKey(key)) {
                String path = o[key].as<String>();
                
                // a. Đổi đuôi .enc -> .bin
                if (path.endsWith(".enc")) {
                    path.replace(".enc", ".bin");
                }
                
                // b. [FIX] Thêm dấu / vào đầu nếu thiếu (Quan trọng!)
                if (path.length() > 0 && path.charAt(0) != '/') {
                    path = "/" + path;
                }
                
                o[key] = path; // Ghi đè lại vào JSON object
            }
        };

        // Áp dụng cho 3 trường đường dẫn
        fix_path(obj, "path");
        fix_path(obj, "path_bootloader");
        fix_path(obj, "path_partition");
        
        // Debug nhẹ để kiểm tra
        // const char* id = obj["fw_id"];
        // const char* p  = obj["path"];
        // ESP_LOGI(TAG, "Fixed Path [%s]: %s", id, p);
    }

    // 4. Ghi nội dung đã sửa vào file chính /index.txt
    if (SD.exists("/index.txt")) SD.remove("/index.txt");
    
    File fileMain = SD.open("/index.txt", FILE_WRITE);
    if (!fileMain) {
        ESP_LOGE(TAG, "Failed to open /index.txt for writing");
        return false;
    }

    if (serializeJson(doc, fileMain) == 0) {
        ESP_LOGE(TAG, "Failed to write content to /index.txt");
        fileMain.close();
        return false;
    }
    fileMain.close();

    // 5. Dọn dẹp
    SD.remove(FILE_IDX_TMP);
    
    ESP_LOGI(TAG, "Index updated successfully.");
    return true;
}

// ============================================================
// HÀM CHÍNH: SYNC ENGINE
// ============================================================
void sync_engine_run(bool force_clean) {
    ESP_LOGI(TAG, ">>> START SYNC ENGINE <<<");
    // [MỚI] Nếu yêu cầu Xóa sạch (Reset gốc)
    if (force_clean) {
        ESP_LOGW(TAG, "FORCE CLEAN ACTIVATED! Wiping all firmware...");
        oled_show_message("Resetting...", "Wiping SD");
        vTaskDelay(pdMS_TO_TICKS(500));
        // Duyệt map hiện tại để xóa folder
        for (auto const& [id, meta] : g_firmware_map) {
            String p = String(meta.path.c_str());
            int slash = p.lastIndexOf('/');
            if (slash > 0) {
                String dir = "/" + p.substring(0, slash);
                // Hàm rmdir đệ quy (cần viết thêm nếu thư viện SD chuẩn không hỗ trợ xóa non-empty)
                // Nhưng thường chỉ cần xóa file index.txt là lần sau nó tự coi là thẻ trống
            }
        }
        
        // Cách nhanh nhất: Xóa file index.txt -> Coi như mất trí nhớ
        SD.remove("/index.txt");
        g_firmware_map.clear(); // Xóa RAM
        ESP_LOGI(TAG, "Wipe Done. Starting fresh sync...");
        oled_show_message("Wipe Done", "Re-Syncing");
        delay(1000);
    }
    oled_show_message("Syncing...", "Init...");

    // 1. Lấy Config
    static char url_cfg[256], key[33], iv[33];
    wifi_config_get_params(url_cfg, key, iv);
    String baseUrl = String(url_cfg);

    if (baseUrl.length() < 10) {
        ESP_LOGE(TAG, "Invalid URL Config");
        oled_show_message("Error", "No URL Config");
        return;
    }

    // 2. Tải Index Server -> Lưu vào FILE_IDX_TMP (File tên ngắn)
    oled_show_message("Syncing...", "DL Index");
    
    // Xóa file tạm cũ nếu còn sót
    if (SD.exists(FILE_IDX_TMP)) SD.remove(FILE_IDX_TMP);

    // Tải về
    std::string json_std = ota_download_index(url_cfg, FILE_IDX_TMP);
    
    if (json_std.length() == 0) {
        ESP_LOGE(TAG, "Download Index Failed");
        oled_show_message("Error", "DL Index Fail");
        // Dọn dẹp nếu file rác được tạo ra
        if (SD.exists(FILE_IDX_TMP)) SD.remove(FILE_IDX_TMP);
        return;
    }

    // 3. Parse Index Server
    oled_show_message("Syncing...", "Parsing");
    FirmwareMap server_map;
    // Parse từ chuỗi RAM (json_std)
    if (!metadata_parse_json(String(json_std.c_str()), server_map)) {
        oled_show_message("Error", "Bad JSON");
        SD.remove(FILE_IDX_TMP); // Xóa file lỗi
        return;
    }

    FirmwareMap& local_map = g_firmware_map;
    bool data_changed = false;

    // 4. VÒNG LẶP QUYẾT ĐỊNH
    int count = 0;
    for (auto& [id, sv_meta] : server_map) {
        count++;
        char msgBuf[32];
        snprintf(msgBuf, sizeof(msgBuf), "Check %d/%d", count, server_map.size());
        oled_show_message("Syncing...", msgBuf);

        bool need_dl_app = false;
        bool need_dl_boot = false;
        bool need_dl_part = false;
        
        // --- [FIX] HÀM KIỂM TRA FILE THÔNG MINH ---
        // Input: path từ server (vd: phuc01/FW.enc)
        // Logic: Đổi .enc -> .bin, Thêm / -> Check SD
        auto is_file_missing = [](String path) -> bool {
            if (path.length() == 0) return false; // Không có path thì ko tính là missing
            
            // 1. Đổi đuôi .enc thành .bin (Vì trên thẻ lưu là .bin)
            if (path.endsWith(".enc")) path.replace(".enc", ".bin");
            
            // 2. Thêm dấu / vào đầu nếu thiếu
            if (!path.startsWith("/")) path = "/" + path;
            
            // 3. Kiểm tra file có tồn tại không
            return !SD.exists(path.c_str());
        };

        // --- LOGIC SO SÁNH ---
        if (local_map.find(id) == local_map.end()) {
            ESP_LOGW(TAG, "[%s] NEW -> Download ALL", id.c_str());
            need_dl_app = true;
            if (!sv_meta.path_bootloader.empty()) need_dl_boot = true;
            if (!sv_meta.path_partition.empty()) need_dl_part = true;
        } else {
            firmware_metadata_t local = local_map[id];

            // 1. APP: So sánh MD5 HOẶC Kiểm tra file .bin có mất không
            if (sv_meta.md5 != local.md5) {
                ESP_LOGI(TAG, "[%s] MD5 Diff -> Update App", id.c_str());
                need_dl_app = true;
            } 
            else if (is_file_missing(String(sv_meta.path.c_str()))) {
                ESP_LOGW(TAG, "[%s] File .bin Missing -> Repair App", id.c_str());
                need_dl_app = true;
            }
            
            // 2. BOOTLOADER
            if (!sv_meta.path_bootloader.empty()) {
                if (sv_meta.md5_bootloader != local.md5_bootloader || 
                    is_file_missing(String(sv_meta.path_bootloader.c_str()))) {
                    need_dl_boot = true;
                }
            }
            
            // 3. PARTITION
            if (!sv_meta.path_partition.empty()) {
                if (sv_meta.md5_partition != local.md5_partition || 
                    is_file_missing(String(sv_meta.path_partition.c_str()))) {
                    need_dl_part = true;
                }
            }
        }

        // --- HÀM TẢI (LAMBDA) ---
        auto download_item = [&](String url_rel, const char* item_name) {
            if (url_rel.length() < 3) return;
            
            // 1. URL tải: Giữ nguyên .enc để tải từ Server
            String dlUrl = build_download_url(baseUrl, url_rel);
            
            // 2. Path lưu: Ban đầu lấy từ JSON (đang là .enc)
            // Thêm dấu "/" vào đầu nếu thiếu
            String savePath = (url_rel[0] == '/') ? url_rel : "/" + url_rel;
            
            // [QUAN TRỌNG] Đổi đuôi .enc -> .bin
            // Vì file lưu xuống thẻ là file đã giải mã (binary)
            if (savePath.endsWith(".enc")) {
                savePath.replace(".enc", ".bin");
            }

            oled_show_message("DL...", item_name);
            
            // 3. Gọi hàm tải:
            // - Tải từ dlUrl (.enc)
            // - Lưu vào savePath (.bin)
            if (ota_download_file_encrypted(dlUrl.c_str(), savePath.c_str(), key, iv)) {
                data_changed = true;
            } else {
                ESP_LOGE(TAG, "[%s] Failed DL %s", id.c_str(), item_name);
            }
        };

        if (need_dl_app)  download_item(String(sv_meta.path.c_str()), sv_meta.path.c_str());
        if (need_dl_boot) download_item(String(sv_meta.path_bootloader.c_str()), sv_meta.path_bootloader.c_str());
        if (need_dl_part) download_item(String(sv_meta.path_partition.c_str()), sv_meta.path_partition.c_str());
    }

    // 5. DỌN DẸP
    oled_show_message("Syncing...", "Cleaning");
    std::vector<std::string> to_delete;
    for (auto const& [id, local_meta] : local_map) {
        if (server_map.find(id) == server_map.end()) {
            to_delete.push_back(id);
        }
    }
    for (const auto& id : to_delete) {
        firmware_metadata_t meta = local_map[id];
        if (SD.exists(("/" + meta.path).c_str())) SD.remove(("/" + meta.path).c_str());
        // ... Xóa các file khác nếu cần
        
        // Xóa folder cha
        String pathStr = String(meta.path.c_str());
        int slash = pathStr.lastIndexOf('/');
        if (slash > 0) {
            String dir = "/" + pathStr.substring(0, slash);
            SD.rmdir(dir);
        }
        data_changed = true;
    }

    // 6. KẾT THÚC
    if (data_changed) {
        // Cập nhật bằng cách đổi tên file tạm -> /index.txt
        if (update_index_file()) {
            sd_load_metadata(); // Reload RAM
            oled_show_message("Sync Done", "Updated!");
        } else {
            oled_show_message("Sync Error", "Save Fail");
        }
    } else {
        SD.remove(FILE_IDX_TMP); // Xóa file tạm vì không dùng
        ESP_LOGI(TAG, "System Up-to-date.");
        oled_show_message("Sync Done", "Up-to-date");
    }
    
    vTaskDelay(2000);
}