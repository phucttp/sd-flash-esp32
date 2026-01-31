
#ifndef __FLASHER_H__
#define __FLASHER_H__

#include "../sd_card/sd_card.h" // Phải include để dùng struct kia
#include <string>

// === CÁC ĐỊA CHỈ NẠP CHUẨN CỦA ESP32 ===
// (Đây là các giá trị mặc định của esptool)

#define ESP_BOOTLOADER_ADDR 0x1000   // Địa chỉ nạp Bootloader
#define ESP_PARTITION_ADDR  0x8000   // Địa chỉ nạp Bảng phân vùng
#define ESP_APPLICATION_ADDR 0x10000 // Địa chỉ nạp App (app0)
#define BUFFER_SIZE 4096            // Kích thước buffer đọc/ghi (4KB)
#define FLASH_UART_TX_PIN      GPIO_NUM_0   // TX từ ESP Host → RX Target
#define FLASH_UART_RX_PIN      GPIO_NUM_1   // RX từ ESP Host ← TX Target
#define FLASH_RESET_PIN        GPIO_NUM_2   // Điều khiển RESET của Target
#define FLASH_BOOT_PIN         GPIO_NUM_3   // Điều khiển GPIO0 của Target (BOOT mode)

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

/**
 * @brief Hiển thị thông báo và khởi động lại ESP32 Host.
 */
void host_system_restart();

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

#endif // __FLASHER_H__