
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
/*
 * @brief Cấu trúc mô tả một "công việc" nạp file.
 * Nó cho biết nạp file NÀO, vào ĐÂU, và kiểm tra bằng MD5 NÀO.
 */
typedef struct {
    uint32_t    file_size;                  // Kích thước file firmware
    uint32_t    buffer_size;                // Kích thước buffer đọc/ghi
    uint32_t    address_bootloader;        // Địa chỉ nạp trên thiết bị target
    uint32_t    address_partition_table;   // Địa chỉ nạp trên thiết bị target
    uint32_t    address_application;       // Địa chỉ nạp trên thiết bị target
} flash_job_t;


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
#endif // __FLASHER_H__