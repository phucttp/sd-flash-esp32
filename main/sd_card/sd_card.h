/**
 * @file sd_card.h
 * @brief Header file này định nghĩa các hàm và cấu trúc
 * để quản lý thẻ SD và metadata của firmware cho ESP32.
 */

#pragma once // Bộ bảo vệ, đảm bảo file này chỉ được include 1 lần

//===== CÁC THƯ VIỆN CẦN THIẾT =====
#include "Arduino.h" // Thư viện lõi Arduino
#include "FS.h"      // Thư viện File System (Hệ thống file)
#include "SD.h"      // Thư viện giao tiếp thẻ SD
#include <string>   // Thư viện C++ cho std::string
#include <map>      // Thư viện C++ cho std::map (để lưu metadata)
#include "../firmware_types.h" // Định nghĩa firmware_metadata_t và FirmwareMap
// Lưu ý: Kiểu esp_err_t được định nghĩa bên trong các header của ESP-IDF,
// thường đã được "Arduino.h" (cho ESP32) include sẵn.

//===== BIẾN TOÀN CỤC (KHAI BÁO) =====

/**
 * @brief Cờ (flag) toàn cục báo trạng thái mount thẻ SD.
 * (Được *định nghĩa* và gán giá trị trong file .cpp tương ứng)
 * - true: Thẻ SD đã được mount và sẵn sàng.
 * - false: Thẻ SD chưa mount hoặc bị lỗi.
 */
extern bool g_is_sd_mounted;

//===== ĐỊNH NGHĨA CẤU TRÚC =====

/**
 * @brief Cấu trúc (struct) để lưu trữ thông tin metadata
 * của một phiên bản firmware cụ thể.
 */
// typedef struct {
//     std::string device_type;      // Loại thiết bị (ví dụ: "ESP32-S3-DevKit")
//     std::string version;          // Phiên bản firmware (ví dụ: "1.0.2")
//     std::string path;             // Đường dẫn tới file .bin chính của firmware
//     std::string md5;              // Mã MD5 hash của file .bin chính
//     std::string path_bootloader;  // Đường dẫn tới file bootloader.bin (nếu có)
//     std::string md5_bootloader;   // Mã MD5 của file bootloader
//     std::string path_partition;   // Đường dẫn tới file partitions.bin (nếu có)
//     std::string md5_partition;    // Mã MD5 của file partitions
// } firmware_metadata_t;

//===== BIẾN TOÀN CỤC (KHAI BÁO) =====

/**
 * @brief Bản đồ (Map) C++ lưu trữ toàn bộ metadata đọc được từ thẻ SD.
 * (Được *định nghĩa* trong file .cpp tương ứng)
 * - KEY: (std::string) ID của firmware (ví dụ: "FW_ESP32_S3_V1.0.2")
 * - VALUE: (firmware_metadata_t) Cấu trúc chứa toàn bộ thông tin.
 */
extern std::map<std::string, firmware_metadata_t> g_firmware_map;

//===== KHAI BÁO HÀM (PROTOTYPES) =====

/**
 * @brief Khởi tạo giao tiếp SPI và mount thẻ SD.
 * @param cs_pin Chân GPIO được sử dụng làm chân Chip Select (CS) cho thẻ SD.
 * @return 
 * - ESP_OK: Mount thẻ SD thành công.
 * - Mã lỗi của esp_err_t (ví dụ: ESP_FAIL) nếu thất bại.
 */
esp_err_t sd_mount(int cs_pin);

/**
 * @brief Unmount thẻ SD và giải phóng tài nguyên.
 * @return 
 * - ESP_OK: Unmount thành công.
 * - Mã lỗi của esp_err_t nếu thất bại.
 */
esp_err_t sd_unmount();

/**
 * @brief Đọc file metadata (ví dụ: "metadata.json") từ thẻ SD.
 * Hàm này sẽ parse file và nạp dữ liệu vào biến toàn cục `g_firmware_map`.
 * @return 
 * - ESP_OK: Đọc và parse file thành công.
 * - ESP_FAIL: Lỗi khi mở file.
 * - ESP_ERR_INVALID_ARG: Lỗi khi parse JSON.
 */
esp_err_t sd_load_metadata();

/**
 * @brief Tìm kiếm và lấy thông tin metadata của một firmware dựa trên ID.
 * Hàm này sẽ tra cứu trong `g_firmware_map`.
 *
 * @param fw_id (const std::string&) ID của firmware cần tìm (KEY trong map).
 * @param out_metadata (firmware_metadata_t&) Tham chiếu đến một struct
 * để lưu kết quả tìm được (nếu hàm trả về ESP_OK).
 * @return
 * - ESP_OK: Tìm thấy firmware và `out_metadata` đã được cập nhật.
 * - ESP_ERR_NOT_FOUND: Không tìm thấy firmware nào có ID tương ứng.
 */
esp_err_t sd_get_firmware_path(const std::string& fw_id, firmware_metadata_t& out_metadata);

// 3. (MỚI) KHAI BÁO 2 HÀM LẤY MENU
/**
 * @brief Lấy mảng con trỏ chứa tên hiển thị menu.
 * @param out_count Biến tham chiếu để trả về số lượng mục.
 * @return Con trỏ tới mảng (const char**).
 */
const char** sd_get_menu_display_items(int& out_count);

/**
 * @brief Lấy mảng con trỏ chứa ID firmware tương ứng.
 * @return Con trỏ tới mảng (const char**).
 */
const char** sd_get_menu_id_items();

/**
 * @brief Đọc description của firmware từ file (lazy load, không lưu RAM)
 * @param fw_id ID của firmware cần lấy description
 * @return String chứa description, rỗng nếu không tìm thấy
 */
String sd_get_description(const char* fw_id);

// ============================================================
// FLASH HISTORY
// ============================================================

/**
 * @brief Ghi fw_id vào cuối history file. Giữ tối đa 10 entries.
 * Gọi sau mỗi lần flash thành công, trước khi restart.
 */
void sd_history_add(const char* fw_id);

/**
 * @brief Đọc history file vào bộ nhớ, xây dựng display strings "#N DevType vVer".
 * Phải gọi SAU sd_load_metadata() vì cần g_firmware_map để lookup tên.
 */
void sd_history_load();

/**
 * @brief Lấy mảng display strings cho History tab (most recent first).
 * @param out_count Số entries.
 * @return Con trỏ tới mảng "#1 STM32 v1.2.3", ...
 */
const char** sd_history_get_display(int& out_count);

/**
 * @brief Lấy mảng fw_id tương ứng với history display items.
 * @return Con trỏ tới mảng fw_id strings.
 */
const char** sd_history_get_ids();

// ============================================================
// METADATA UPDATE (REMOTE FIX)
// ============================================================

/**
 * @brief Cập nhật trường md5/md5_bootloader/md5_partition cho một fw_id
 *        trong /index.txt trên SD card VÀ trong g_firmware_map bộ nhớ.
 *        Truyền nullptr để bỏ qua trường đó.
 * @return ESP_OK nếu thành công, ESP_ERR_NOT_FOUND nếu fw_id không tồn tại.
 */
esp_err_t sd_update_fw_md5(const char* fw_id,
                             const char* md5,
                             const char* md5_bootloader,
                             const char* md5_partition);