#ifndef OTA_DOWNLOADER_H
#define OTA_DOWNLOADER_H

#include <string>
#include "esp_err.h"

// Hàm tải file text (đã có)
std::string ota_download_index(const char* url, const char* save_path = NULL);

/**
 * @brief Tải file mã hóa, giải mã AES-128-CBC và lưu xuống thẻ nhớ.
 * * @param url Đường dẫn tải file (.enc)
 * @param save_path Đường dẫn lưu file sạch (.bin) trên thẻ nhớ
 * @param key Chuỗi Key 16 ký tự
 * @param iv Chuỗi IV 16 ký tự
 * @return true Nếu thành công
 * @return false Nếu lỗi
 */
bool ota_download_file_encrypted(const char* url, const char* save_path, const char* key, const char* iv);

#endif // OTA_DOWNLOADER_H