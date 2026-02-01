/**
 * @file metadata_parser.cpp
 * @brief Triển khai logic parse JSON sử dụng thư viện ArduinoJson.
 */

// ============================================================
// INCLUDES
// ============================================================
#include "metadata_parser.h"
#include <ArduinoJson.h>
#include "esp_log.h"

static const char* TAG = "JSON_PARSER";

// ============================================================
// HÀM CÔNG KHAI
// ============================================================

/**
 * @brief Parse chuỗi JSON thành FirmwareMap
 * @param json_content Nội dung JSON
 * @param out_map Output map chứa metadata
 * @return true nếu parse thành công
 */
bool metadata_parse_json(const String& json_content, FirmwareMap& out_map) {
    ESP_LOGI(TAG, ">>> Bắt đầu phân tích JSON (Kích thước: %d bytes)...", json_content.length());

    // 1. TÍNH TOÁN VÀ CẤP PHÁT BỘ NHỚ ĐỆM (JSON DOCUMENT)
    // -------------------------------------------------------------------
    // Firmware list có thể dài, nên cần bộ đệm đủ lớn.
    // 10KB (20480 bytes) là đủ cho khoảng 10-15 firmware với đầy đủ thông tin.
    // Nếu danh sách dài hơn, cần tăng số này lên (lưu ý RAM của ESP32).
    const size_t CAPACITY = 20 * 1024; // 10KB
    
    // Sử dụng DynamicJsonDocument để cấp phát trên Heap (RAM động)
    DynamicJsonDocument doc(CAPACITY);

    // 2. GIẢI MÃ (DESERIALIZE)
    // -------------------------------------------------------------------
    // Chuyển chuỗi String thành cây dữ liệu JSON Object
    DeserializationError error = deserializeJson(doc, json_content);

    // Kiểm tra lỗi cú pháp JSON (ví dụ: thiếu ngoặc, sai định dạng)
    if (error) {
        ESP_LOGE(TAG, "LỖI: Parse JSON thất bại! Lý do: %s", error.c_str());
        return false; // Dừng ngay nếu lỗi
    }

    // 3. CHUẨN BỊ DỮ LIỆU ĐẦU RA
    // -------------------------------------------------------------------
    // Xóa sạch bản đồ cũ để đảm bảo không bị trộn lẫn với dữ liệu mới
    out_map.clear();

    // Ép kiểu root thành mảng (Array) vì cấu trúc index.txt là [...]
    JsonArray root = doc.as<JsonArray>();
    int count = 0; // Biến đếm số lượng firmware hợp lệ

    // 4. DUYỆT TỪNG PHẦN TỬ (ITERATE)
    // -------------------------------------------------------------------
    for (JsonObject obj : root) {
        // Lấy ID (Khóa chính). Nếu không có ID -> Bỏ qua bản ghi rác này
        const char* fw_id = obj["fw_id"];
        if (!fw_id) {
            ESP_LOGW(TAG, "Cảnh báo: Phát hiện mục không có 'fw_id', bỏ qua.");
            continue; 
        }

        // Tạo một struct tạm để hứng dữ liệu
        firmware_metadata_t meta;

        // --- [A] LẤY THÔNG TIN CƠ BẢN ---
        // Cú pháp: obj["key"] | "default_value"
        // Tác dụng: Nếu trong JSON thiếu trường đó, nó sẽ tự điền giá trị mặc định (chuỗi rỗng)
        // giúp chương trình không bị crash hoặc null pointer.
        meta.device_type = obj["device_type"] | "Unknown_Device";
        meta.version     = obj["version"]     | "0.0.0";

        // --- [B] LẤY THÔNG TIN APP (FIRMWARE CHÍNH) ---
        meta.path = obj["path"] | ""; // Đường dẫn file .bin
        meta.md5  = obj["md5"]  | ""; // Mã hash MD5

        // --- [C] LẤY THÔNG TIN BOOTLOADER (Tùy chọn) ---
        meta.path_bootloader = obj["path_bootloader"] | "";
        meta.md5_bootloader  = obj["md5_bootloader"]  | "";

        // --- [D] LẤY THÔNG TIN PARTITION TABLE (Tùy chọn) ---
        meta.path_partition  = obj["path_partition"]  | "";
        meta.md5_partition   = obj["md5_partition"]   | "";

        // --- [E] LẤY THÔNG TIN MÃ HÓA (Tùy chọn) ---
        meta.encrypted = obj["encrypted"] | false;  // Default: false for backward compat

        // 5. NẠP VÀO MAP
        // -------------------------------------------------------------------
        // Key: fw_id (std::string)
        // Value: meta (struct)
        // Lưu ý: Chuyển const char* sang std::string để lưu trữ an toàn lâu dài
        out_map[std::string(fw_id)] = meta;
        
        count++;
        // Log nhẹ (Debug level) để biết đang xử lý cái nào
        ESP_LOGD(TAG, "Đã nạp: %s (v%s)", fw_id, meta.version.c_str());
    }

    // 6. KẾT THÚC
    // -------------------------------------------------------------------
    ESP_LOGI(TAG, "Phân tích hoàn tất. Tổng cộng: %d firmware hợp lệ.", count);
    
    // Giải phóng bộ nhớ doc (tự động khi ra khỏi scope hàm này)
    return true;
}