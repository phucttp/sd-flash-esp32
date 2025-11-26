/*
 * Module: wifi_config.h
 * Chức năng: Quản lý các cấu hình kết nối WiFi và cấu hình URL server. 
*/
#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <Arduino.h>

// Định nghĩa đường dẫn file cấu hình
#define CONFIG_FILE_URL  "/config/url.txt"
#define CONFIG_FILE_KEY  "/config/aes_key.txt"
#define CONFIG_FILE_IV   "/config/aes_iv.txt"

// Giá trị mặc định (Nếu thẻ nhớ chưa có)
#define DEFAULT_URL "https://raw.githubusercontent.com/DEFAULT/REPO/main/fw.enc"
#define DEFAULT_KEY "1234567890123456" // 16 ký tự
#define DEFAULT_IV  "0000000000000000" // 16 ký tự

/**
 * @brief Kết nối Wifi (AutoConnect) và lấy URL Firmware
 * * @param out_url_buffer: Buffer để nhận Link Github trả về (để truyền cho hàm Download sau này)
 * @param buffer_size: Kích thước tối đa của buffer
 * @return true: Nếu kết nối thành công
 * @return false: Nếu lỗi hoặc timeout
 */

bool wifi_config_connect();

/**
 * @brief Buộc vào chế độ cấu hình Wifi (bỏ qua AutoConnect)
 * @param out_url_buffer: Buffer để nhận Link Github trả về (để truyền cho hàm Download sau này)
 * @param buffer_size: Kích thước tối đa của buffer
 */
void wifi_config_force_portal();

/**
 * @brief Hàm tiện ích: Đọc cấu hình từ thẻ nhớ ra buffer để sử dụng
 * (Dùng khi bắt đầu tải file)
 */
void wifi_config_get_params(char* out_url, char* out_key, char* out_iv);

/**
 * @brief Ngắt kết nối Wifi, tắt Radio
 */
void wifi_config_stop();

#endif // WIFI_CONFIG_H