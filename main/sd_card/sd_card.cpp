/**
 * @file sd_card.cpp
 * @brief Quản lý SD card — đọc/ghi metadata firmware, index, cấu hình và lịch sử nạp.
 *
 * Chức năng chính:
 *   - Khởi tạo và quản lý kết nối SD card qua SPI
 *   - Đọc và ghi file index.txt (danh sách firmware, thứ tự = thứ tự hiển thị trên OLED)
 *   - Load metadata từng firmware: device_type, version, description, md5, path
 *   - Lưu và nạp lịch sử nạp firmware (/flash_history.txt, tối đa 10 mục)
 *   - Cung cấp dữ liệu cho màn hình OLED: tên hiển thị, mô tả, danh sách ID
 */

// ============================================================
// INCLUDES
// ============================================================
#include "esp_log.h"
#include "sd_card.h"
#include "ArduinoJson.h"
#include <SD.h>
#include <vector>
#include <map>

// ============================================================
// BIẾN TOÀN CỤC
// ============================================================
static const char *TAG = "SD_CARD";
static const char *TAG1 = "SD_METADATA";
const char *METADATA_FILE_PATH = "/index.txt";
bool g_is_sd_mounted = false;

// Bản đồ lưu trữ metadata firmware
std::map<std::string, firmware_metadata_t> g_firmware_map;

// Vector tĩnh để lưu trữ mảng menu
static std::vector<std::string> g_displayStrings;
static std::vector<std::string> g_idStrings;
static std::vector<const char*> g_menuDisplayItemsPtrs;
static std::vector<const char*> g_menuFirmwareIDsPtrs;

// Vector tĩnh cho flash history
static std::vector<std::string> g_histIdStrings;
static std::vector<std::string> g_histDisplayStrings;
static std::vector<const char*> g_histIdPtrs;
static std::vector<const char*> g_histDisplayPtrs;

static const char* HISTORY_FILE_PATH = "/history.txt";
static const int   HISTORY_MAX_ENTRIES = 10;

// ============================================================
// HÀM CÔNG KHAI
// ============================================================

/**
 * @brief Khởi tạo giao tiếp SD card
 * @param cs_pin Chân CS của SPI
 * @return ESP_OK nếu thành công
 */
esp_err_t sd_mount(int cs_pin) {
    if (!SD.begin(cs_pin, SPI, 40000000)) { // Thử 40MHz
        ESP_LOGW(TAG, "Failed to mount at 40MHz, trying 20MHz...");
        if (!SD.begin(cs_pin, SPI, 20000000)) { // Fallback về 20MHz nếu thất bại
             ESP_LOGE(TAG, "Card Mount Failed at 20MHz too");
             g_is_sd_mounted = false;
             return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "SD initialized OK!");
    g_is_sd_mounted = true;
    return ESP_OK;
}

/**
 * @brief Giải phóng tài nguyên SD card
 */
esp_err_t sd_unmount() {
    SD.end();
    ESP_LOGI(TAG, "SD Unmounted");
    return ESP_OK;
}

/**
 * @brief Đọc metadata firmware từ index.txt trên SD card
 */
esp_err_t sd_load_metadata() {
    // Kiểm tra SD đã mount chưa
    if (!g_is_sd_mounted) {
        ESP_LOGE(TAG1, "SD Card not mounted");
        return ESP_FAIL;
    }

    // [FIX] Xóa menu cũ ngay từ đầu
    g_firmware_map.clear();
    g_displayStrings.clear();
    g_idStrings.clear();
    g_menuDisplayItemsPtrs.clear();
    g_menuFirmwareIDsPtrs.clear();

    //2. Mở file INDEX.JSON từ thẻ SD
    File metadataFile = SD.open(METADATA_FILE_PATH, FILE_READ);

    // [FIX] Nếu file không tồn tại -> Tạo file mới với JSON rỗng
    if (!metadataFile) {
        ESP_LOGW(TAG1, "Index file not found, creating empty one...");
        File newFile = SD.open(METADATA_FILE_PATH, FILE_WRITE);
        if (newFile) {
            newFile.print("[]"); // JSON array rỗng
            newFile.close();
            ESP_LOGI(TAG1, "Created empty index file: %s", METADATA_FILE_PATH);
        }
        // Tiếp tục chạy với firmware_map rỗng
        goto build_menu_pointers;
    }

    // 3. Phân tích cú pháp JSON
    {
        // Cấp phát bộ nhớ động cho bộ đệm JSON
        const size_t JSON_BUFFER_SIZE = 20 * 1024; // 20KB
        DynamicJsonDocument doc(JSON_BUFFER_SIZE);
        DeserializationError error = deserializeJson(doc, metadataFile);
        metadataFile.close();

        if (error) {
            ESP_LOGE(TAG1, "Failed to parse metadata JSON: %s", error.c_str());
            // [FIX] Parse lỗi -> vẫn tiếp tục với menu rỗng
            goto build_menu_pointers;
        }

        // 4. Lưu trữ metadata vào bản đồ trong RAM
        JsonArray root = doc.as<JsonArray>();

        int i = 1;
        for (JsonObject firmware_obj : root) {
            const char* fw_id = firmware_obj["fw_id"];
            if(!fw_id){
                ESP_LOGW(TAG1, "Firmware entry without fw_id, skipping");
                continue;
            }
            firmware_metadata_t metadata = {
                .device_type = firmware_obj["device_type"] | "",
                .version = firmware_obj["version"] | "",
                .path = firmware_obj["path"] | "",
                .md5 = firmware_obj["md5"] | "",
                .path_bootloader = firmware_obj["path_bootloader"] | "",
                .md5_bootloader = firmware_obj["md5_bootloader"] | "",
                .path_partition = firmware_obj["path_partition"] | "",
                .md5_partition = firmware_obj["md5_partition"] | "",
                .encrypted = firmware_obj["encrypted"] | false
            };
            g_firmware_map[fw_id] = metadata;
            // Đồng thời tạo menu
            std::string displayName = std::to_string(i) + ". " + metadata.device_type + " " + metadata.version;
            g_displayStrings.push_back(displayName); // Lưu chuỗi
            g_idStrings.push_back(fw_id);            // Lưu ID
            i++;
            ESP_LOGD(TAG1, "Tải FW: %s (Path: %s)", fw_id, metadata.path.c_str());
        }
    }

build_menu_pointers:
    // Tạo mảng con trỏ cho menu UI
    {
        // Tạo mảng con trỏ từ firmware list
        for (const auto& s : g_displayStrings) {
            g_menuDisplayItemsPtrs.push_back(s.c_str());
        }
        for (const auto& s : g_idStrings) {
            g_menuFirmwareIDsPtrs.push_back(s.c_str());
        }

        // Nếu không có firmware nào, thêm placeholder
        if (g_displayStrings.empty()) {
            g_displayStrings.push_back("(No Firmware)");
            g_idStrings.push_back("NO_FW");
            g_menuDisplayItemsPtrs.push_back(g_displayStrings.back().c_str());
            g_menuFirmwareIDsPtrs.push_back(g_idStrings.back().c_str());
        }
    }

    ESP_LOGI(TAG, "Tải Metadata hoàn tất. Tổng cộng %d firmware được tải.", g_firmware_map.size());
    return ESP_OK;
}

/**
 * @brief Lấy metadata của firmware theo fw_id
 * @param fw_id ID của firmware cần tìm
 * @param out_metadata Output metadata
 * @return ESP_OK nếu tìm thấy
 */
esp_err_t sd_get_firmware_path(const std::string& fw_id, firmware_metadata_t& out_metadata) {
    // Kiểm tra SD đã mount chưa
    if (!g_is_sd_mounted) {
        ESP_LOGE(TAG1, "SD Card not mounted");
        return ESP_FAIL;
    }

    // Tìm kiếm fw_id trong bản đồ metadata
    auto it = g_firmware_map.find(fw_id);
    if (it == g_firmware_map.end()) {
        ESP_LOGE(TAG1, "Firmware ID %s not found in metadata", fw_id.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    // Lấy path firmware
    out_metadata = it->second;
    ESP_LOGI(TAG1, "Firmware ID %s found: Path=%s, Version=%s",
             fw_id.c_str(), out_metadata.path.c_str(), out_metadata.version.c_str());
    return ESP_OK;
}

// ============================================================
// HÀM LẤY MENU
// ============================================================

/**
 * @brief Lấy danh sách hiển thị menu firmware
 * @param out_count Số lượng item
 * @return Mảng con trỏ đến tên hiển thị
 */
const char** sd_get_menu_display_items(int& out_count) {
    out_count = g_menuDisplayItemsPtrs.size();
    return g_menuDisplayItemsPtrs.data();
}

/**
 * @brief Lấy danh sách fw_id tương ứng với menu
 * @return Mảng con trỏ đến fw_id
 */
const char** sd_get_menu_id_items() {
    return g_menuFirmwareIDsPtrs.data();
}

// ============================================================
// HÀM LẤY DESCRIPTION (LAZY LOAD)
// ============================================================

/**
 * @brief Đọc description của firmware từ file index.txt (không lưu RAM)
 * @param fw_id ID của firmware cần lấy description
 * @return String chứa description, rỗng nếu không tìm thấy
 */
String sd_get_description(const char* fw_id) {
    if (!g_is_sd_mounted || !fw_id) return "";

    File f = SD.open(METADATA_FILE_PATH, FILE_READ);
    if (!f) {
        ESP_LOGW(TAG1, "Cannot open index file for description");
        return "";
    }

    // Buffer nhỏ, chỉ đủ parse 1 lần
    DynamicJsonDocument doc(8 * 1024);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        ESP_LOGE(TAG1, "JSON parse error: %s", err.c_str());
        return "";
    }

    // Tìm firmware theo fw_id
    for (JsonObject obj : doc.as<JsonArray>()) {
        const char* id = obj["fw_id"];
        if (id && strcmp(id, fw_id) == 0) {
            // Ưu tiên root level, fallback sang cloud_source
            const char* desc = obj["description"];
            if (!desc || strlen(desc) == 0) {
                desc = obj["cloud_source"]["description"];
            }
            return desc ? String(desc) : "";
        }
    }

    return "";
}

// ============================================================
// FLASH HISTORY
// ============================================================

void sd_history_add(const char* fw_id) {
    if (!g_is_sd_mounted || !fw_id || strlen(fw_id) == 0) return;

    // Đọc entries hiện có
    std::vector<std::string> entries;
    File f = SD.open(HISTORY_FILE_PATH, FILE_READ);
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() > 0) entries.push_back(line.c_str());
        }
        f.close();
    }

    // Append entry mới
    entries.push_back(fw_id);

    // Giữ tối đa HISTORY_MAX_ENTRIES (trim cũ nhất)
    if ((int)entries.size() > HISTORY_MAX_ENTRIES) {
        entries.erase(entries.begin(),
                      entries.begin() + (entries.size() - HISTORY_MAX_ENTRIES));
    }

    // Ghi lại file
    SD.remove(HISTORY_FILE_PATH);
    f = SD.open(HISTORY_FILE_PATH, FILE_WRITE);
    if (f) {
        for (auto& e : entries) f.println(e.c_str());
        f.close();
        ESP_LOGI(TAG, "History: added '%s' (%d entries)", fw_id, (int)entries.size());
    } else {
        ESP_LOGE(TAG, "History: cannot write %s", HISTORY_FILE_PATH);
    }
}

void sd_history_load() {
    g_histIdStrings.clear();
    g_histDisplayStrings.clear();
    g_histIdPtrs.clear();
    g_histDisplayPtrs.clear();

    if (!g_is_sd_mounted) return;

    // Đọc tất cả entries từ file
    std::vector<std::string> entries;
    File f = SD.open(HISTORY_FILE_PATH, FILE_READ);
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() > 0) entries.push_back(line.c_str());
        }
        f.close();
    }

    if (entries.empty()) return;

    // Build display strings, most recent (cuối file) = #1
    int count = (int)entries.size();
    for (int i = 0; i < count; i++) {
        const std::string& fw_id = entries[count - 1 - i];  // Đảo ngược: mới nhất trước
        g_histIdStrings.push_back(fw_id);

        char disp[24];
        auto it = g_firmware_map.find(fw_id);
        if (it != g_firmware_map.end()) {
            snprintf(disp, sizeof(disp), "#%d %s v%s",
                     i + 1, it->second.device_type.c_str(), it->second.version.c_str());
        } else {
            snprintf(disp, sizeof(disp), "#%d %s", i + 1, fw_id.c_str());
        }
        g_histDisplayStrings.push_back(disp);
    }

    for (auto& s : g_histIdStrings)     g_histIdPtrs.push_back(s.c_str());
    for (auto& s : g_histDisplayStrings) g_histDisplayPtrs.push_back(s.c_str());

    ESP_LOGI(TAG, "History loaded: %d entries", count);
}

const char** sd_history_get_display(int& out_count) {
    out_count = (int)g_histDisplayPtrs.size();
    return out_count > 0 ? g_histDisplayPtrs.data() : nullptr;
}

const char** sd_history_get_ids() {
    return g_histIdPtrs.empty() ? nullptr : g_histIdPtrs.data();
}

// ============================================================
// METADATA UPDATE (REMOTE FIX)
// ============================================================

esp_err_t sd_update_fw_md5(const char* fw_id,
                             const char* md5,
                             const char* md5_bootloader,
                             const char* md5_partition) {
    if (!g_is_sd_mounted || !fw_id) return ESP_FAIL;

    // 1. Read existing index.txt into JSON document
    File f = SD.open(METADATA_FILE_PATH, FILE_READ);
    if (!f) {
        ESP_LOGE(TAG1, "sd_update_fw_md5: cannot open index");
        return ESP_FAIL;
    }
    DynamicJsonDocument doc(20 * 1024);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        ESP_LOGE(TAG1, "sd_update_fw_md5: JSON parse error: %s", err.c_str());
        return ESP_FAIL;
    }

    // 2. Find the entry and patch only the non-null fields
    bool found = false;
    for (JsonObject obj : doc.as<JsonArray>()) {
        const char* id = obj["fw_id"];
        if (!id || strcmp(id, fw_id) != 0) continue;
        if (md5            && strlen(md5)            == 32) obj["md5"]            = md5;
        if (md5_bootloader && strlen(md5_bootloader) == 32) obj["md5_bootloader"] = md5_bootloader;
        if (md5_partition  && strlen(md5_partition)  == 32) obj["md5_partition"]  = md5_partition;
        found = true;
        break;
    }
    if (!found) {
        ESP_LOGE(TAG1, "sd_update_fw_md5: fw_id '%s' not in index", fw_id);
        return ESP_ERR_NOT_FOUND;
    }

    // 3. Write patched JSON back to SD
    SD.remove(METADATA_FILE_PATH);
    File wf = SD.open(METADATA_FILE_PATH, FILE_WRITE);
    if (!wf) {
        ESP_LOGE(TAG1, "sd_update_fw_md5: cannot write index");
        return ESP_FAIL;
    }
    serializeJson(doc, wf);
    wf.close();

    // 4. Patch in-memory map so the next flash call uses the new values
    auto it = g_firmware_map.find(fw_id);
    if (it != g_firmware_map.end()) {
        if (md5            && strlen(md5)            == 32) it->second.md5            = md5;
        if (md5_bootloader && strlen(md5_bootloader) == 32) it->second.md5_bootloader = md5_bootloader;
        if (md5_partition  && strlen(md5_partition)  == 32) it->second.md5_partition  = md5_partition;
    }

    ESP_LOGI(TAG1, "sd_update_fw_md5: updated '%s' OK", fw_id);
    return ESP_OK;
}