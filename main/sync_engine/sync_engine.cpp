/**
 * @file sync_engine.cpp
 * @brief Đồng bộ firmware từ git repository về SD card.
 *
 * Chức năng chính:
 *   - Tải file index.txt từ git repo, so sánh với index hiện có trên SD card
 *   - Xác định danh sách firmware mới hoặc thay đổi cần tải về (dựa trên MD5)
 *   - Gọi ota_downloader để tải từng file firmware (.bin hoặc .enc) xuống SD card
 *   - Cập nhật index.txt trên SD card sau khi đồng bộ xong
 *   - Hiển thị tiến trình từng bước lên màn hình OLED
 */

#include "sync_engine.h"

// ============================================================
// INCLUDES
// ============================================================
#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "esp_log.h"

#include "../firmware_types.h"
#include "../wifi_config/wifi_config.h"
#include "../ota_downloader/ota_downloader.h"
#include "../metadata_parser/metadata_parser.h"
#include "../sd_card/sd_card.h"
#include "oled_ui.h"
#include "../file_utils/file_utils.h"  // Centralized file operations

extern OledUI ui;                   

static const char* TAG = "SYNC_ENGINE";

// ============================================================
// ĐỊNH NGHĨA TÊN FILE
// ============================================================
// File tạm để tải về (Tên phải ngắn < 8 ký tự để tránh lỗi FAT32)
#define FILE_IDX_TMP   "/idx_new.txt" 

// ============================================================
// HÀM PHỤ TRỢ
// ============================================================

/**
 * @brief Tạo URL đầy đủ từ base URL và relative path
 */
String build_download_url(String baseUrl, String relPath) {
    int lastSlash = baseUrl.lastIndexOf('/');
    String rootUrl = (lastSlash > 0) ? baseUrl.substring(0, lastSlash + 1) : baseUrl + "/";
    return rootUrl + relPath;
}

/**
 * @brief Cập nhật file index (đọc tạm -> thêm "/" vào path -> ghi file chính)
 * @note Không convert .enc -> .bin nữa, giữ nguyên .enc trên SD
 * @return true nếu thành công
 */
bool update_index_file() {
    ESP_LOGI(TAG, "Updating index paths (adding '/' prefix)...");

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
        fu_file_delete(FILE_IDX_TMP);
        return false;
    }

    // 3. Duyệt mảng và sửa đổi đường dẫn
    JsonArray root = doc.as<JsonArray>();
    for (JsonObject obj : root) {
        
        // Lambda sửa đường dẫn: Chỉ thêm dấu / vào đầu (giữ nguyên .enc)
        // [OLD] Trước đây convert .enc -> .bin ở đây, giờ KHÔNG làm nữa
        auto fix_path = [](JsonObject& o, const char* key) {
            if (o.containsKey(key)) {
                String path = o[key].as<String>();

                // Chỉ thêm dấu / vào đầu nếu thiếu
                // Giữ nguyên đuôi .enc - giải mã khi flash, không phải khi sync
                if (path.length() > 0 && path.charAt(0) != '/') {
                    path = "/" + path;
                }

                o[key] = path;
            }
        };

        // Áp dụng cho 3 trường đường dẫn
        fix_path(obj, "path");
        fix_path(obj, "path_bootloader");
        fix_path(obj, "path_partition");

        // [NEW] Đánh dấu firmware này là encrypted (vì file trên SD là .enc)
        obj["encrypted"] = true;
        
        // Debug nhẹ để kiểm tra
        // const char* id = obj["fw_id"];
        // const char* p  = obj["path"];
        // ESP_LOGI(TAG, "Fixed Path [%s]: %s", id, p);
    }

    // 4. Ghi nội dung đã sửa vào file chính /index.txt
    fu_file_delete("/index.txt"); // Xóa file cũ nếu có
    
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
    fu_file_delete(FILE_IDX_TMP);
    
    ESP_LOGI(TAG, "Index updated successfully.");
    return true;
}

// ============================================================
// HÀM CHÍNH
// ============================================================

/**
 * @brief Chạy sync engine - đồng bộ firmware từ server về SD card
 * @param force_clean Xóa sạch firmware cũ trước khi sync
 */
void sync_engine_run(bool force_clean) {
    ESP_LOGI(TAG, ">>> START SYNC ENGINE <<<");
    // [MỚI] Nếu yêu cầu Xóa sạch (Reset gốc)
    if (force_clean) {
        ESP_LOGW(TAG, "FORCE CLEAN ACTIVATED! Wiping all firmware...");
        ui.showMessage("Resetting...", "Wiping SD");
        vTaskDelay(pdMS_TO_TICKS(500));

        // [FIX] Xóa THẬT SỰ tất cả firmware files
        for (auto const& [id, meta] : g_firmware_map) {
            ESP_LOGI(TAG, "Deleting firmware: %s", id.c_str());

            // Xóa app file
            if (!meta.path.empty()) {
                fu_file_delete(meta.path.c_str());
            }
            // Xóa bootloader file
            if (!meta.path_bootloader.empty()) {
                fu_file_delete(meta.path_bootloader.c_str());
            }
            // Xóa partition file
            if (!meta.path_partition.empty()) {
                fu_file_delete(meta.path_partition.c_str());
            }

            // Xóa folder chứa (nếu empty)
            std::string parent = fu_get_parent_dir(meta.path.c_str());
            if (!parent.empty()) {
                fu_dir_delete(parent.c_str()); // Chỉ xóa nếu empty
            }
        }

        // Xóa index.txt
        fu_file_delete("/index.txt");
        g_firmware_map.clear(); // Xóa RAM

        ESP_LOGI(TAG, "Wipe Done. Starting fresh sync...");
        ui.showMessage("Wipe Done", "Re-Syncing");
        delay(1000);
    }
    ui.showMessage("Syncing...", "Init...");

    // 1. Lấy Config
    static char url_cfg[256], key[33], iv[33];
    wifi_config_get_params(url_cfg, key, iv);
    String baseUrl = String(url_cfg);

    if (baseUrl.length() < 10) {
        ESP_LOGE(TAG, "Invalid URL Config");
        ui.showMessage("Error", "No URL Config");
        return;
    }

    // 2. Tải Index Server -> Lưu vào FILE_IDX_TMP (File tên ngắn)
    ui.showMessage("Syncing...", "DL Index");
    
    // Xóa file tạm cũ nếu còn sót
    fu_file_delete(FILE_IDX_TMP);

    // Tải về
    std::string json_std = ota_download_index(url_cfg, FILE_IDX_TMP);
    
    if (json_std.length() == 0) {
        ESP_LOGE(TAG, "Download Index Failed");
        ui.showMessage("Error", "DL Index Fail");
        // Dọn dẹp nếu file rác được tạo ra
        fu_file_delete(FILE_IDX_TMP);
        return;
    }

    // 3. Parse Index Server
    ui.showMessage("Syncing...", "Parsing");
    FirmwareMap server_map;
    // Parse từ chuỗi RAM (json_std)
    if (!metadata_parse_json(String(json_std.c_str()), server_map)) {
        ui.showMessage("Error", "Bad JSON");
        fu_file_delete(FILE_IDX_TMP); // Xóa file lỗi
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
        ui.showMessage("Syncing...", msgBuf);

        bool need_dl_app = false;
        bool need_dl_boot = false;
        bool need_dl_part = false;
        
        // --- [FIX] HÀM KIỂM TRA FILE THÔNG MINH ---
        // Input: path từ server (vd: phuc01/FW.enc)
        // Logic: Giữ nguyên .enc (vì file trên SD cũng là .enc), Thêm / -> Check SD
        auto is_file_missing = [](String path) -> bool {
            if (path.length() == 0) return false; // Không có path thì ko tính là missing

            // [FIX] KHÔNG đổi .enc -> .bin vì file trên SD giờ lưu là .enc
            // Chỉ cần thêm / vào đầu nếu thiếu
            String checkPath = (path[0] == '/') ? path : "/" + path;

            // Kiểm tra file có tồn tại không
            return !fu_file_exists(checkPath.c_str());
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
        // [MỚI] Giữ nguyên file .enc trên thẻ SD, giải mã khi flash
        auto download_item = [&](String url_rel, const char* item_name) {
            if (url_rel.length() < 3) return;

            // 1. URL tải: Giữ nguyên .enc để tải từ Server
            String dlUrl = build_download_url(baseUrl, url_rel);

            // 2. Path lưu: Giữ nguyên .enc (KHÔNG đổi sang .bin)
            // Thêm dấu "/" vào đầu nếu thiếu
            String savePath = (url_rel[0] == '/') ? url_rel : "/" + url_rel;

            // [MỚI] KHÔNG đổi đuôi - giữ nguyên .enc
            // File sẽ được giải mã khi flash, không phải khi sync

            ui.showMessage("DL...", item_name);

            // 3. Gọi hàm tải THÔ (không giải mã):
            // - Tải từ dlUrl (.enc)
            // - Lưu vào savePath (.enc) - giữ nguyên mã hóa
            if (ota_download_file_raw(dlUrl.c_str(), savePath.c_str())) {
                data_changed = true;
            } else {
                ESP_LOGE(TAG, "[%s] Failed DL %s", id.c_str(), item_name);
            }
        };

        if (need_dl_app)  download_item(String(sv_meta.path.c_str()), sv_meta.path.c_str());
        if (need_dl_boot) download_item(String(sv_meta.path_bootloader.c_str()), sv_meta.path_bootloader.c_str());
        if (need_dl_part) download_item(String(sv_meta.path_partition.c_str()), sv_meta.path_partition.c_str());
    }

    // 5. DỌN DẸP - Xóa firmware không còn trên server
    ui.showMessage("Syncing...", "Cleaning");
    std::vector<std::string> to_delete;
    for (auto const& [id, local_meta] : local_map) {
        if (server_map.find(id) == server_map.end()) {
            to_delete.push_back(id);
        }
    }
    for (const auto& id : to_delete) {
        firmware_metadata_t meta = local_map[id];
        ESP_LOGW(TAG, "Removing obsolete firmware: %s", id.c_str());

        // Xóa file theo path từ local index
        if (!meta.path.empty()) {
            ESP_LOGI(TAG, "  Delete: %s", meta.path.c_str());
            fu_file_delete(meta.path.c_str());
        }
        if (!meta.path_bootloader.empty()) {
            ESP_LOGI(TAG, "  Delete: %s", meta.path_bootloader.c_str());
            fu_file_delete(meta.path_bootloader.c_str());
        }
        if (!meta.path_partition.empty()) {
            ESP_LOGI(TAG, "  Delete: %s", meta.path_partition.c_str());
            fu_file_delete(meta.path_partition.c_str());
        }

        // Xóa folder cha (nếu empty)
        std::string parent = fu_get_parent_dir(meta.path.c_str());
        if (!parent.empty()) {
            ESP_LOGI(TAG, "  Delete folder: %s", parent.c_str());
            fu_dir_delete(parent.c_str());
        }

        data_changed = true;
    }

    // 6. KẾT THÚC - LUÔN cập nhật index.txt từ server (source of truth)
    // Dù không tải file mới, metadata (device_type, version, order...) vẫn cần sync
    if (update_index_file()) {
        sd_load_metadata(); // Reload RAM
        if (data_changed) {
            ui.showMessage("Sync Done", "Files Updated!");
        } else {
            ui.showMessage("Sync Done", "Up-to-date");
        }
    } else {
        ui.showMessage("Sync Error", "Save Fail");
    }
    
    vTaskDelay(2000);
}