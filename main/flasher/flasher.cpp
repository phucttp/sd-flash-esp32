// #include "../sd_card/sd_card.h" // Đã được include trong "flasher.h" rồi, nên comment lại là đúng!
#include <inttypes.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp32_port.h"      // Header "port" của thư viện esp_loader, cung cấp hàm loader_port_esp32_init
#include "esp_loader.h"      // Header chính của thư viện esp_loader
#include "FS.h"
#include "SD.h"
#include "Arduino.h"
#include "flasher.h"
#include "../oled/menu.h"    // Thêm để dùng hàm oled_show_message

// TAG dùng để lọc log cho module này
static const char *TAG = "FLASHER";

/**
 * @brief Khởi tạo phần cứng của HOST (ESP32-C3) để giao tiếp với TARGET.
 * * Hàm này cài đặt các chân UART (TX/RX) và các chân điều khiển
 * (RTS/DTR) để thư viện esp_loader có thể nói chuyện được với target.
 * * CHẠY 1 LẦN DUY NHẤT lúc khởi động.
 */
 // Cấu hình các chân GPIO và cổng UART mà HOST sẽ dùng
const loader_esp32_config_t config = {
    .baud_rate = 115200,             // Tốc độ baud giao tiếp
    .uart_port = UART_NUM_1,         // Dùng UART1 của con HOST
    .uart_rx_pin = GPIO_NUM_1,      // Chân RX của HOST (nối với TX của Target)
    .uart_tx_pin = GPIO_NUM_0,      // Chân TX của HOST (nối với RX của Target)
    .reset_trigger_pin = GPIO_NUM_2,// Chân HOST điều khiển chân EN/RESET của Target
    .gpio0_trigger_pin = GPIO_NUM_3, // Chân HOST điều khiển chân BOOT/GPIO0 của Target
};

static esp_err_t reset_sequence(const loader_esp32_config_t *config);

esp_err_t flasher_init() {
//     // Cấu hình các chân GPIO và cổng UART mà HOST sẽ dùng
//    const loader_esp32_config_t config = {
//       .baud_rate = 115200,             // Tốc độ baud giao tiếp
//       .uart_port = UART_NUM_1,         // Dùng UART1 của con HOST
//       .uart_rx_pin = GPIO_NUM_1,      // Chân RX của HOST (nối với TX của Target)
//       .uart_tx_pin = GPIO_NUM_0,      // Chân TX của HOST (nối với RX của Target)
//       .reset_trigger_pin = GPIO_NUM_2,// Chân HOST điều khiển chân EN/RESET của Target
//       .gpio0_trigger_pin = GPIO_NUM_3, // Chân HOST điều khiển chân BOOT/GPIO0 của Target
//    };

    // Đăng ký cấu hình phần cứng này với "driver port" của thư viện
   if (loader_port_esp32_init(&config) != ESP_LOADER_SUCCESS) {
      ESP_LOGE(TAG, "serial initialization failed.");
      return ESP_FAIL;
   }

   ESP_LOGI(TAG, "UART connection initialized at baud rate 115200");
   return ESP_OK;
}

esp_err_t flasher_write_segment(const std::string& file_path, uint32_t offset, const std::string& md5)
{
    ESP_LOGI(TAG, "==== Writing segment ====");
    ESP_LOGI(TAG, "File: %s | Offset: 0x%08" PRIx32, file_path.c_str(), offset);

    // --- MỞ FILE ---
    File fwFile = SD.open(file_path.c_str(), FILE_READ);
    if (!fwFile) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    size_t total_size = fwFile.size();
    if (total_size == 0) {
        ESP_LOGE(TAG, "File is empty: %s", file_path.c_str());
        fwFile.close();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Segment size: %zu bytes", total_size);

    // --- BẮT ĐẦU GHI FLASH ---
    esp_loader_error_t err = esp_loader_flash_start(offset, total_size, BUFFER_SIZE);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "Failed to start flash for segment. err=%d", err);
        fwFile.close();
        return ESP_FAIL;
    }

    uint8_t* buffer = (uint8_t*) malloc(BUFFER_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer!");
        fwFile.close();
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_written = 0;
    size_t bytes_read = 0;

    while ((bytes_read = fwFile.read(buffer, BUFFER_SIZE)) > 0) {
        err = esp_loader_flash_write(buffer, bytes_read);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "Write error at offset %zu (err=%d)", bytes_written, err);
            free(buffer);
            fwFile.close();
            return ESP_FAIL;
        }

        bytes_written += bytes_read;
        ESP_LOGI(TAG, "Progress: %" PRIu32 "%%", (uint32_t)((bytes_written * 100) / total_size));
        oled_show_message(file_path.c_str(), (String("Progress: ") + String((bytes_written * 100) / total_size) + String("%")).c_str());
    }

    free(buffer);
    fwFile.close();

    ESP_LOGI(TAG, "Segment written %zu / %zu bytes OK", bytes_written, total_size);

    // --- KIỂM TRA MD5 (NẾU CÓ) ---
    if (md5.length() == 32) {
        char md5_ascii[33];
        strncpy(md5_ascii, md5.c_str(), 32);
        md5_ascii[32] = '\0';

        err = esp_loader_flash_verify_known_md5(offset, total_size, (const uint8_t*) md5_ascii);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "MD5 check failed for segment!");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "MD5 verified OK for segment!");
    } else {
        ESP_LOGW(TAG, "No valid MD5 provided, skipping verification.");
    }

    ESP_LOGI(TAG, "Segment at 0x%08" PRIx32 " written successfully.", offset);
    return ESP_OK;
}


esp_err_t flasher_begin_session(const std::string& fw_id)
{
    firmware_metadata_t metadata;

    // --- BƯỚC 1: LẤY THÔNG TIN FILE TỪ SD CARD ---
    esp_err_t ret = sd_get_firmware_path(fw_id, metadata);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get firmware metadata for fw_id: %s", fw_id.c_str());
        return ret;
    }

    // Thực hiện chuỗi reset để target vào bootloader cái này khá quan trọng
    // --- BƯỚC 2: HANDSHAKE ---
    esp_loader_connect_args_t connect_config = ESP_LOADER_CONNECT_DEFAULT();
    // connect_config.sync_timeout = 2000;
    reset_sequence(&config); 

    if (esp_loader_connect(&connect_config) != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "Failed to connect to target device.");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Connected to target device.");
    
    // esp_loader_error_t err_erase = esp_loader_flash_erase();
    // if (err_erase != ESP_LOADER_SUCCESS) {
    //     ESP_LOGE(TAG, "Flash erase failed. err=%d", err_erase);
    //     return ESP_FAIL;
    // }

    // --- BƯỚC 3: BOOST BAUDRATE ---
    uint32_t new_baud = 460800;
    esp_loader_error_t err_baud = esp_loader_change_transmission_rate(new_baud);
    if (err_baud == ESP_LOADER_SUCCESS) {
        uart_set_baudrate(UART_NUM_1, new_baud);
        ESP_LOGI(TAG, "Baudrate boosted to %lu", new_baud);
    } else {
        ESP_LOGW(TAG, "Baudrate boost failed, continue at 115200");
    }


    // --- BƯỚC 4: GHI TỪNG PHÂN VÙNG ---
    // Tùy firmware, ông đổi file_path + offset cho đúng
    ret = flasher_write_segment(metadata.path_bootloader,0x1000, metadata.md5_bootloader);
    if (ret != ESP_OK) return ret;

    ret = flasher_write_segment(metadata.path_partition, 0x8000,metadata.md5_partition);
    if (ret != ESP_OK) return ret;

    ret = flasher_write_segment(metadata.path, 0x10000, metadata.md5);
    if (ret != ESP_OK) return ret;

    // --- BƯỚC 5: RESET TARGET ---
    esp_loader_reset_target();
    ESP_LOGI(TAG, "Resetting target to run app...");
    gpio_set_level(GPIO_NUM_3, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(GPIO_NUM_2, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(GPIO_NUM_2, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Target restarted in normal mode.");
    ESP_LOGI(TAG, "Full firmware update completed successfully!");
    sd_unmount(); // Giải phóng thẻ SD sau khi nạp xong
    return ESP_OK;
}

// Thực hiện chuỗi thao tác reset target
static esp_err_t reset_sequence(const loader_esp32_config_t *config)
{
    esp_err_t ret;

    // Cấu hình GPIO
    ret = gpio_set_direction((gpio_num_t)config->gpio0_trigger_pin, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) return ret;

    ret = gpio_set_direction((gpio_num_t)config->reset_trigger_pin, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Reset sequence start...");

    // Bắt đầu quy trình reset
    gpio_set_level((gpio_num_t)config->gpio0_trigger_pin, 0);   // giữ GPIO0 LOW
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_set_level((gpio_num_t)config->reset_trigger_pin, 0);   // kéo EN LOW
    vTaskDelay(pdMS_TO_TICKS(100));

    gpio_set_level((gpio_num_t)config->reset_trigger_pin, 1);   // nhả EN HIGH
    vTaskDelay(pdMS_TO_TICKS(120));

    gpio_set_level((gpio_num_t)config->gpio0_trigger_pin, 1);   // nhả GPIO0 HIGH
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Reset sequence done!");
    return ESP_OK;
}
