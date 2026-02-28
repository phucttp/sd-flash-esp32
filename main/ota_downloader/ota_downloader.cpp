/**
 * @file ota_downloader.cpp
 * @brief Tải file firmware từ URL HTTP/HTTPS về SD card trong quá trình đồng bộ.
 *
 * Chức năng chính:
 *   - Tải file từ URL có hỗ trợ HTTPS (sử dụng ESP certificate bundle tích hợp)
 *   - Thêm cache-buster ngẫu nhiên vào URL để tránh nhận file cũ từ CDN hoặc proxy
 *   - Ghi dữ liệu theo từng block 1KB trực tiếp xuống SD card (không đệm toàn bộ vào RAM)
 *   - Hiển thị tiến trình tải lên màn hình OLED theo phần trăm
 */

#include "ota_downloader.h"

// ============================================================
// INCLUDES
// ============================================================
#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "mbedtls/aes.h"
#include "esp_system.h"
#include "oled_ui.h"

extern OledUI ui;

// ============================================================
// BIẾN TOÀN CỤC
// ============================================================
static const char* TAG = "OTA_DL";

#define MAX_HTTP_RECV_BUFFER 1024
#define DL_BUFFER_SIZE 1024

// ============================================================
// HÀM HỖ TRỢ
// ============================================================

/**
 * @brief Thêm tham số ngẫu nhiên vào URL để tránh cache
 */
String add_cache_buster(const char* original_url) {
    String url = String(original_url);
    long random_val = esp_random(); // Lấy số ngẫu nhiên phần cứng
    
    // Kiểm tra xem URL đã có tham số chưa
    if (url.indexOf('?') == -1) {
        url += "?cb=" + String(random_val); // cb = cache buster
    } else {
        url += "&cb=" + String(random_val);
    }
    return url;
}

// ============================================================
// HÀM CÔNG KHAI
// ============================================================

/**
 * @brief Tải file index (JSON) từ server
 * @param url URL của file index
 * @param save_path Đường dẫn lưu file trên SD (NULL = không lưu)
 * @return Nội dung JSON dạng string
 */
std::string ota_download_index(const char* url, const char* save_path) {
    // [FIX] Tạo URL mới có đuôi chống cache
    String freshUrl = add_cache_buster(url);
    
    ESP_LOGI(TAG, "Downloading Index: %s", freshUrl.c_str()); // Log URL mới

    std::string payload = ""; 

    esp_http_client_config_t config = {};
    config.url = freshUrl.c_str(); // <--- DÙNG URL MỚI TẠI ĐÂY
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.disable_auto_redirect = false;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init http client");
        return "";
    }

    // Mở kết nối
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        // Chỉ khi kết nối thành công mới xử lý tiếp
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status = %d, Content-Length = %d", status_code, content_length);

        if (status_code == 200) {
            char *buffer = (char*)malloc(MAX_HTTP_RECV_BUFFER + 1);
            if (buffer != NULL) {
                // Vòng lặp đọc dữ liệu
                while (1) {
                    int read_len = esp_http_client_read(client, buffer, MAX_HTTP_RECV_BUFFER);
                    if (read_len <= 0) {
                         if (esp_http_client_is_complete_data_received(client)) {
                            ESP_LOGI(TAG, "Download finished.");
                        }
                        break; 
                    }
                    buffer[read_len] = 0; // Null-terminate
                    payload.append(buffer);
                }
                free(buffer);

                // --- GHI FILE (Nếu có yêu cầu) ---
                if (save_path != NULL && strlen(save_path) > 0) {
                    if (SD.exists(save_path)) SD.remove(save_path);
                    File file = SD.open(save_path, FILE_WRITE);
                    if (file) {
                        file.print(payload.c_str());
                        file.close();
                        ESP_LOGI(TAG, "Saved content to SD: %s", save_path);
                    } else {
                        ESP_LOGE(TAG, "Failed to open %s for writing", save_path);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Cannot malloc buffer");
            }
        } else {
            ESP_LOGE(TAG, "HTTP Error Code: %d", status_code);
        }
        
        // Đóng kết nối
        esp_http_client_close(client);
    } else {
        ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(err));
    }

    // Dọn dẹp client
    esp_http_client_cleanup(client);
    
    return payload;
}

/**
 * @brief Tải file mã hóa từ server và giải mã AES-128-CBC
 * @param url URL file mã hóa
 * @param save_path Đường dẫn lưu file đã giải mã
 * @param key Key AES 16 bytes
 * @param iv IV AES 16 bytes
 * @return true nếu thành công
 */
bool ota_download_file_encrypted(const char* url, const char* save_path, const char* key, const char* iv) {
    // [FIX] Tạo URL mới có đuôi chống cache
    String freshUrl = add_cache_buster(url);

    ESP_LOGI(TAG, "Start Secure Download: %s -> %s", freshUrl.c_str(), save_path);

    if (strlen(key) != 16 || strlen(iv) != 16) {
        ESP_LOGE(TAG, "Key/IV length invalid! Must be 16 chars.");
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = freshUrl.c_str(); // <--- DÙNG URL MỚI TẠI ĐÂY
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.disable_auto_redirect = false;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Init HTTP failed");
        return false;
    }

    bool success = false;

    // Mở kết nối
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);

        if (status_code == 200) {

            // --- [MỚI] LOGIC TỰ TẠO THƯ MỤC ---
            // Tách đường dẫn thư mục từ save_path và tạo nếu chưa có
            char *pathStr = strdup(save_path);
            if (pathStr) {
                char *ptr = strrchr(pathStr, '/');
                if (ptr) {
                    *ptr = 0; // Cắt tên file đi
                    if (strlen(pathStr) > 0 && !SD.exists(pathStr)) {
                        ESP_LOGW(TAG, "Creating missing dir: %s", pathStr);
                        SD.mkdir(pathStr);
                    }
                }
                free(pathStr);
            }
            // ----------------------------------

            // Chuẩn bị file
            if (SD.exists(save_path)) SD.remove(save_path);
            File file = SD.open(save_path, FILE_WRITE);
            
            if (file) {
                // Cấp phát bộ nhớ
                char *buffer_in = (char*)malloc(DL_BUFFER_SIZE);
                char *buffer_out = (char*)malloc(DL_BUFFER_SIZE);
                
                if (buffer_in && buffer_out) {
                    // Cấu hình AES
                    mbedtls_aes_context aes;
                    mbedtls_aes_init(&aes);
                    mbedtls_aes_setkey_dec(&aes, (const unsigned char*)key, 128);
                    
                    unsigned char iv_temp[16];
                    memcpy(iv_temp, iv, 16);

                    int total_written = 0;
                    int last_kb = 0;
                    
                    // Vòng lặp xử lý
                    while (1) {
                        int read_len = esp_http_client_read(client, buffer_in, DL_BUFFER_SIZE);
                        if (read_len > 0) {
                            int blocks = read_len / 16;
                            if (read_len % 16 != 0) {
                                 ESP_LOGW(TAG, "Warning: Data chunk not aligned 16 bytes (%d)", read_len);
                            }

                            if (blocks > 0) {
                                mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, blocks * 16, 
                                                      iv_temp, 
                                                      (const unsigned char*)buffer_in, 
                                                      (unsigned char*)buffer_out);
                                
                                file.write((uint8_t*)buffer_out, blocks * 16);
                                total_written += (blocks * 16);
                            }
                           // --- HIỂN THỊ PROGRESS BAR ---
                            if (content_length > 0) {
                                int percent = (total_written * 100) / content_length;
                                if (percent != last_kb) {  // Dùng last_kb làm last_percent
                                    last_kb = percent;
                                    ui.showProgress("Downloading...", percent);
                                    if (percent % 10 == 0) {
                                        ESP_LOGI(TAG, "Downloaded %d%%", percent);
                                    }
                                }
                            } else {
                                // Fallback nếu không biết tổng size
                                int current_kb = total_written / 1024;
                                if (current_kb - last_kb >= 50) {
                                    last_kb = current_kb;
                                    String msg = String(current_kb) + " KB";
                                    ui.showMessage("Downloading...", msg.c_str());
                                }
                            }
                            // -------------------------------------------------------------
                        } 
                        else if (read_len == 0) {
                            if (esp_http_client_is_complete_data_received(client)) {
                                ESP_LOGI(TAG, "Download & Decrypt Complete. Size: %d", total_written);
                                success = true;
                            } else {
                                 ESP_LOGE(TAG, "Connection closed unexpectedly");
                            }
                            break;
                        } 
                        else {
                            ESP_LOGE(TAG, "Read error");
                            break;
                        }
                    }
                    // Dọn dẹp AES
                    mbedtls_aes_free(&aes);
                } else {
                    ESP_LOGE(TAG, "Malloc failed");
                }
                
                // Giải phóng bộ nhớ
                if (buffer_in) free(buffer_in);
                if (buffer_out) free(buffer_out);
                file.close();
            } else {
                ESP_LOGE(TAG, "Cannot open file to write: %s", save_path);
            }
        } else {
            ESP_LOGE(TAG, "HTTP Error: %d", status_code);
        }
        // Đóng kết nối
        esp_http_client_close(client);
    } else {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(err));
    }

    // Dọn dẹp client cuối cùng
    esp_http_client_cleanup(client);

    // Nếu thất bại thì xóa file rác
    if (!success && SD.exists(save_path)) {
        SD.remove(save_path);
        ESP_LOGW(TAG, "Deleted incomplete file");
    }

    return success;
}

/**
 * @brief Tải file thô từ server (không giải mã)
 * @param url URL file
 * @param save_path Đường dẫn lưu file
 * @return true nếu thành công
 */
bool ota_download_file_raw(const char* url, const char* save_path) {
    // [FIX] Tạo URL mới có đuôi chống cache
    String freshUrl = add_cache_buster(url);

    ESP_LOGI(TAG, "Start RAW Download: %s -> %s", freshUrl.c_str(), save_path);

    esp_http_client_config_t config = {};
    config.url = freshUrl.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.disable_auto_redirect = false;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Init HTTP failed");
        return false;
    }

    bool success = false;

    // Mở kết nối
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);

        if (status_code == 200) {
            // --- TỰ TẠO THƯ MỤC ---
            char *pathStr = strdup(save_path);
            if (pathStr) {
                char *ptr = strrchr(pathStr, '/');
                if (ptr) {
                    *ptr = 0;
                    if (strlen(pathStr) > 0 && !SD.exists(pathStr)) {
                        ESP_LOGW(TAG, "Creating missing dir: %s", pathStr);
                        SD.mkdir(pathStr);
                    }
                }
                free(pathStr);
            }

            // Chuẩn bị file
            if (SD.exists(save_path)) SD.remove(save_path);
            File file = SD.open(save_path, FILE_WRITE);

            if (file) {
                // Cấp phát bộ nhớ
                char *buffer = (char*)malloc(DL_BUFFER_SIZE);

                if (buffer) {
                    int total_written = 0;
                    int last_kb = 0;

                    // Vòng lặp xử lý - KHÔNG GIẢI MÃ
                    while (1) {
                        int read_len = esp_http_client_read(client, buffer, DL_BUFFER_SIZE);
                        if (read_len > 0) {
                            // Ghi trực tiếp (không giải mã)
                            file.write((uint8_t*)buffer, read_len);
                            total_written += read_len;

                            // Hiển thị progress bar
                            if (content_length > 0) {
                                int percent = (total_written * 100) / content_length;
                                if (percent != last_kb) {  // Dùng last_kb làm last_percent
                                    last_kb = percent;
                                    ui.showProgress("Downloading...", percent);
                                    if (percent % 10 == 0) {
                                        ESP_LOGI(TAG, "Downloaded %d%%", percent);
                                    }
                                }
                            } else {
                                // Fallback nếu không biết size
                                int current_kb = total_written / 1024;
                                if (current_kb - last_kb >= 50) {
                                    last_kb = current_kb;
                                    ui.showMessage("Downloading...", (String(current_kb) + " KB").c_str());
                                }
                            }
                        }
                        else if (read_len == 0) {
                            if (esp_http_client_is_complete_data_received(client)) {
                                ESP_LOGI(TAG, "RAW Download Complete. Size: %d", total_written);
                                success = true;
                            } else {
                                ESP_LOGE(TAG, "Connection closed unexpectedly");
                            }
                            break;
                        }
                        else {
                            ESP_LOGE(TAG, "Read error");
                            break;
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "Malloc failed");
                }

                if (buffer) free(buffer);
                file.close();
            } else {
                ESP_LOGE(TAG, "Cannot open file to write: %s", save_path);
            }
        } else {
            ESP_LOGE(TAG, "HTTP Error: %d", status_code);
        }
        esp_http_client_close(client);
    } else {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    // Xóa file rác nếu thất bại
    if (!success && SD.exists(save_path)) {
        SD.remove(save_path);
        ESP_LOGW(TAG, "Deleted incomplete file");
    }

    return success;
}