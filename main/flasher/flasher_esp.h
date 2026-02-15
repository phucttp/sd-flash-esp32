/**
 * @file flasher_esp.h
 * @brief ESP32 UART flasher engine using esp-serial-flasher library.
 * @details Handles flashing ESP32 targets via UART (bootloader, partition, app).
 */

#ifndef __FLASHER_ESP_H__
#define __FLASHER_ESP_H__

#include "flasher_common.h"
#include "../sd_card/sd_card.h"
#include <string>

// === ĐỊA CHỈ NẠP CHUẨN CỦA ESP32 ===
// (Giá trị mặc định của esptool)
#define ESP_BOOTLOADER_ADDR  0x1000   // Địa chỉ nạp Bootloader
#define ESP_PARTITION_ADDR   0x8000   // Địa chỉ nạp Bảng phân vùng
#define ESP_APPLICATION_ADDR 0x10000  // Địa chỉ nạp App (app0)

/**
 * @brief Khởi tạo phần cứng (UART, GPIO) cho module flasher.
 * Chạy 1 lần duy nhất lúc khởi động.
 */
esp_err_t flasher_init(void);

/**
 * @brief Bắt đầu một phiên nạp firmware vào target.
 * @param job Thông tin về file cần nạp (path, md5, địa chỉ).
 * @return ESP_OK nếu nạp và xác thực thành công.
 */
// esp_err_t flasher_start_update(const std::string& fw_id);
esp_err_t flasher_begin_session(const std::string& fw_id);

esp_err_t flasher_write_segment(const std::string& file_path, uint32_t offset, const std::string& md5 = "");

/**
 * @brief Write an encrypted segment to target flash with real-time decryption.
 * @param file_path Path to .enc file on SD card
 * @param offset Flash offset address
 * @param md5 Expected MD5 hash of decrypted data (optional)
 * @return ESP_OK on success
 *
 * @note Reads AES key/IV from /config/aes_key.txt and /config/aes_iv.txt
 */
esp_err_t flasher_write_segment_encrypted(const std::string& file_path, uint32_t offset, const std::string& md5 = "");

/**
 * @brief Xóa toàn bộ flash của chip Target.
 */
esp_err_t flasher_chip_erase(void);

// ============================================================
// AUTO-RETRY API - Quản lý phiên nạp với retry tự động
// ============================================================

/**
 * @brief Kiểm tra có đang nạp dở không (gọi trong setup sau SD mount).
 * @return true nếu có pending flash cần resume.
 */
bool flasher_has_pending(void);

/**
 * @brief Lấy fw_id đang nạp dở.
 * @return Pointer đến chuỗi fw_id, hoặc "" nếu không có.
 */
const char* flasher_get_pending_fw_id(void);

/**
 * @brief Bắt đầu phiên nạp mới (khởi tạo state từ đầu).
 * @param fw_id Firmware ID cần nạp.
 */
void flasher_start_new_session(const char* fw_id);

// ============================================================
// SCAN BOOT API - Quét và lưu combo kết nối
// ============================================================

/**
 * @brief Quét tất cả 12 combo reset và lưu combo working vào SD.
 * @return Index của combo thành công (0-11), hoặc -1 nếu thất bại.
 */
int flasher_scan_and_save_combo(void);

/**
 * @brief Load combo đã lưu từ SD card.
 * @return Index của combo đã lưu (0-11), hoặc -1 nếu chưa có.
 */
int flasher_load_saved_combo(void);

/**
 * @brief Xóa combo đã lưu (reset về brute-force mặc định).
 */
void flasher_clear_saved_combo(void);

#endif // __FLASHER_ESP_H__