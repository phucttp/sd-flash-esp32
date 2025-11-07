#include "esp_log.h"
#include "sd_card.h"
#include "ArduinoJson.h"
#include <SD.h>
#include <vector> // Cần cho mảng động
#include <map>    // Vẫn dùng map cho get_firmware_path

//Khai báo TAG cho module sd_card
static const char *TAG = "SD_CARD";
static const char *TAG1 = "SD_METADATA";
const char *METADATA_FILE_PATH = "/index.txt";// đường dẫn cố định đến file metadata trên thẻ SD
bool g_is_sd_mounted = false; //Khai báo trạng thái mount thẻ SD

//Bản đồ lưu trữ metadata firmware
std::map<std::string, firmware_metadata_t> g_firmware_map;

// (MỚI) Các vector tĩnh để LƯU TRỮ mảng menu (tốn RAM nhưng dễ code)
static std::vector<std::string> g_displayStrings;
static std::vector<std::string> g_idStrings;
static std::vector<const char*> g_menuDisplayItemsPtrs;
static std::vector<const char*> g_menuFirmwareIDsPtrs;

//Khởi tạo giao tiêp thẻ SD
esp_err_t sd_mount(int cs_pin) {
    if (!SD.begin(cs_pin)) {
        ESP_LOGE(TAG, "Card Mount Failed");
        g_is_sd_mounted = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SD initialized OK!");
    g_is_sd_mounted = true;
    return ESP_OK;
}

//Giải phóng tài nguyên
esp_err_t sd_unmount() {
    SD.end();
    ESP_LOGI(TAG, "SD Unmounted");
    return ESP_OK;
}

//Đọc metadata của thẻ SD
esp_err_t sd_load_metadata(){
    //1. Kiểm tra thẻ SD đã được mount chưa
    if (!g_is_sd_mounted) {
        ESP_LOGE(TAG1, "SD Card not mounted");
        return ESP_FAIL;
    }

    //2. Mở file INDEX.JSON từ thẻ SD
    File metadataFile = SD.open(METADATA_FILE_PATH, FILE_READ);
    if (!metadataFile) {
        ESP_LOGE(TAG1, "Failed to open metadata file");
        return ESP_ERR_NOT_FOUND;
    }

    // 3. Phân tích cú pháp JSON
    // Cấp phát bộ nhớ động cho bộ đệm JSON
    const size_t JSON_BUFFER_SIZE = 10 * 1024; // 10KB
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, metadataFile);
    metadataFile.close(); 
    if (error) {
        ESP_LOGE(TAG1, "Failed to parse metadata JSON: %s", error.c_str());
        return ESP_ERR_INVALID_STATE;
    }
    // 4. Lưu trữ metadata vào bản đồ trong RAM
    g_firmware_map.clear(); 
    JsonArray root = doc.as<JsonArray>();

    // (MỚI) Xóa menu cũ trước khi tạo menu mới
    g_displayStrings.clear();
    g_idStrings.clear();
    g_menuDisplayItemsPtrs.clear();
    g_menuFirmwareIDsPtrs.clear();

    int i = 1;
    for (JsonObject firmware_obj:root) {
        const char* fw_id = firmware_obj["fw_id"];
        if(!fw_id){
            ESP_LOGW(TAG1, "Firmware entry without fw_id, skipping");
            continue;
        }
        firmware_metadata_t metadata = {
            .device_type = firmware_obj["device_type"] |"",
            .version = firmware_obj["version"] | "",
            .path = firmware_obj["path"] | "",
            .md5 = firmware_obj["md5"] | "",
            .path_bootloader = firmware_obj["path_bootloader"] | "",
            .md5_bootloader = firmware_obj["md5_bootloader"] | "",
            .path_partition = firmware_obj["path_partition"] | "",
            .md5_partition = firmware_obj["md5_partition"] | ""
        };
        g_firmware_map[fw_id] = metadata;
        // (MỚI) Đồng thời tạo menu
        std::string displayName = std::to_string(i) + ". " + metadata.device_type + " " + metadata.version;
        g_displayStrings.push_back(displayName); // Lưu chuỗi
        g_idStrings.push_back(fw_id);            // Lưu ID
        i++;
        ESP_LOGD(TAG1, "Tải FW: %s (Path: %s)", fw_id, metadata.path.c_str());
    }
    
    // (MỚI) Thêm mục Exit vào cuối
    g_displayStrings.push_back(std::to_string(i) + ". (Exit)");
    g_idStrings.push_back("NULL");

    // (MỚI) Tạo mảng con trỏ
    for (const auto& s : g_displayStrings) {
        g_menuDisplayItemsPtrs.push_back(s.c_str());
    }
    for (const auto& s : g_idStrings) {
        g_menuFirmwareIDsPtrs.push_back(s.c_str());
    }
    ESP_LOGI(TAG, "Tải Metadata hoàn tất. Tổng cộng %d firmware được tải.", g_firmware_map.size());
    return ESP_OK;
}

//Path firmware theo fw_id
esp_err_t sd_get_firmware_path(const std::string& fw_id, firmware_metadata_t& out_metadata){
    //Kiểm tra thẻ SD đã được mount chưa
    if (!g_is_sd_mounted) {
        ESP_LOGE(TAG1, "SD Card not mounted");
        return ESP_FAIL;
    }

    //Tìm kiếm fw_id trong bản đồ metadata
    auto it = g_firmware_map.find(fw_id);
    if (it == g_firmware_map.end()) {
        ESP_LOGE(TAG1, "Firmware ID %s not found in metadata", fw_id.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    //Lấy path firmware
    out_metadata = it->second;
    ESP_LOGI(TAG1, "Firmware ID %s found: Path=%s, Version=%s", 
             fw_id.c_str(), out_metadata.path.c_str(), out_metadata.version.c_str());
    return ESP_OK;
}

// --- (MỚI) VIẾT 2 HÀM LẤY MENU ---
// 2 hàm này chỉ đơn giản là trả về con trỏ của 2 mảng static ở trên
const char** sd_get_menu_display_items(int& out_count) {
    out_count = g_menuDisplayItemsPtrs.size();
    return g_menuDisplayItemsPtrs.data();
}

const char** sd_get_menu_id_items() {
    return g_menuFirmwareIDsPtrs.data();
}