/**
 * @file prog_esp32.h
 * @brief ESP32 UART flasher engine using esp-serial-flasher library.
 * @details Handles flashing ESP32 targets via UART (bootloader, partition, app).
 */

#ifndef __PROG_ESP32_H__
#define __PROG_ESP32_H__

#include "../prog_common.h"
#include "../../storage/sd_card/sd_card.h"
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
 * @brief Xóa toàn bộ flash của chip Target.
 */
esp_err_t flasher_chip_erase(void);

// ============================================================
// SCAN BOOT API - Quét và lưu combo kết nối (RAM-only)
// ============================================================

int flasher_scan_and_save_combo(void);
int flasher_load_saved_combo(void);
void flasher_clear_saved_combo(void);

#endif // __PROG_ESP32_H__