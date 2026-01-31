#include "ota_downloader.h"

// ============================================================
// 1. INCLUDE ARDUINO & SD TRƯỚC (QUAN TRỌNG: Tránh lỗi xung đột)
// ============================================================
#include <Arduino.h>
#include <SD.h> 
#include <FS.h>

// ============================================================
// 2. INCLUDE ESP-IDF SAU
// ============================================================
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "mbedtls/aes.h"
#include "esp_system.h"
#include "oled_ui.h"

extern OledUI ui;

static const char* TAG = "OTA_DL";

#define MAX_HTTP_RECV_BUFFER 1024 
#define DL_BUFFER_SIZE 1024 

// ============================================================
// 3. HÀM HỖ TRỢ
// ============================================================
// Helper: Thêm tham số ngẫu nhiên để phá Cache
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
// HÀM 1: TẢI FILE TEXT (INDEX/JSON)
// ============================================================
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

// ============================================================
// HÀM 2: TẢI & GIẢI MÃ FILE
// ============================================================
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
        int status_code = esp_http_client_fetch_headers(client);
        status_code = esp_http_client_get_status_code(client);
        
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
                           // --- [LOGIC MỚI] HIỂN THỊ TỐI GIẢN (Mỗi 20KB cập nhật 1 lần) ---
                            int current_kb = total_written / 1024;
                            
                            if (current_kb - last_kb >= 50) { // Thay đổi >= 50KB mới vẽ lại
                                String msg = "Size: " + String(current_kb) + " KB";
                                ui.showMessage("Downloading...", msg.c_str());
                                
                                last_kb = current_kb; // Lưu mốc mới
                                ESP_LOGI(TAG, "Downloaded %d KB", current_kb); // Log nhẹ
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