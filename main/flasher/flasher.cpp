/**
 * @file flasher.cpp
 * @brief Triển khai các chức năng nạp firmware cho chip ESP32 Target.
 * @details File này xử lý giao tiếp cấp thấp qua UART, sử dụng thư viện esp_loader
 * để thực hiện các thao tác: kết nối (handshake), xóa chip, nạp phân vùng
 * và xác thực dữ liệu (MD5).
 * @author TTP27
 */

// ============================================================
// 1. STANDARD & ESP-IDF LIBRARIES
// ============================================================
#include "flasher.h" // Định nghĩa các hằng số và cấu trúc của module flasher

#include <inttypes.h> // Hỗ trợ định dạng PRIx32
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/gpio.h"

// ============================================================
// 2. ESP-SERIAL-FLASHER LIBRARIES
// ============================================================
#include "esp32_port.h" 	 // Cấu hình cổng UART và GPIO cho esp_loader
#include "esp_loader.h" 	 // Các hàm API cấp cao của bootloader ROM

// ============================================================
// 3. PROJECT LIBRARIES (ARDUINO & OTHERS)
// ============================================================
#include "FS.h"
#include "SD.h"
#include "Arduino.h"
#include "../oled/menu.h" 	 // Hỗ trợ hiển thị tiến trình lên màn hình OLED

// TAG dùng để lọc log cho module này
static const char *TAG = "FLASHER";

// ============================================================
// TIMING PROFILES - Cho các loại chip/mạch khác nhau
// ============================================================
typedef struct {
	uint32_t reset_hold_ms;   // Thời gian giữ EN=LOW
	uint32_t boot_hold_ms;    // Thời gian chờ sau khi thả EN (IO0 vẫn LOW)
	const char* name;
} timing_profile_t;

static const timing_profile_t TIMING_PROFILES[] = {
	{  50,  50, "ULTRA_FAST" },     // Rất nhanh
	{ 100, 100, "FAST" },           // Nhanh
	{ 100, 150, "NORMAL" },         // Chuẩn
	{ 150, 200, "MEDIUM" },         // Trung bình
	{ 150, 300, "SLOW" },           // Chậm, boot_hold dài hơn
	{  80, 120, "SHORT" },          // Reset ngắn
};
#define NUM_PROFILES (sizeof(TIMING_PROFILES) / sizeof(TIMING_PROFILES[0]))

/**
 * @brief Khởi tạo phần cứng của HOST (ESP32-C3) để giao tiếp với TARGET.
 * Hàm này cài đặt các chân UART (TX/RX) và các chân điều khiển
 * (RTS/DTR) để thư viện esp_loader có thể nói chuyện được với target.
 * CHẠY 1 LẦN DUY NHẤT lúc khởi động.
 */
// Cấu hình các chân GPIO và cổng UART mà HOST sẽ dùng
const loader_esp32_config_t config = {
	.baud_rate = 115200, 	 	 // Tốc độ baud giao tiếp ban đầu
	.uart_port = UART_NUM_1, 	 // Dùng UART1 của con HOST (ESP32-C3)
	.uart_rx_pin = FLASH_UART_RX_PIN, 	 // Chân RX của HOST (nối với TX của Target)
	.uart_tx_pin = FLASH_UART_TX_PIN, 	 // Chân TX của HOST (nối với RX của Target)
	.reset_trigger_pin = FLASH_RESET_PIN, // Chân HOST điều khiển chân EN/RESET của Target
	.gpio0_trigger_pin = FLASH_BOOT_PIN, // Chân HOST điều khiển chân BOOT/GPIO0 của Target
};

// Khai báo hàm
static esp_err_t reset_sequence_with_profile(const loader_esp32_config_t *config, const timing_profile_t *profile);
static esp_err_t try_connect(void);

/**
 * @brief Khởi tạo giao tiếp UART và cổng nạp.
 * @return ESP_OK nếu thành công, ESP_FAIL nếu thất bại.
 */
esp_err_t flasher_init() {
	ESP_LOGI(TAG, "Initializing UART connection for flasher...");
	// Khởi tạo UART và cấu hình GPIO theo `config` cho thư viện esp_loader
	if (loader_port_esp32_init(&config) != ESP_LOADER_SUCCESS) {
		ESP_LOGE(TAG, "serial initialization failed.");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "UART connection initialized at baud rate 115200");
	return ESP_OK;
}

/**
 * @brief Ghi một phân vùng (segment) của firmware lên chip Target.
 * @param file_path Đường dẫn file binary trên thẻ nhớ SD.
 * @param offset Địa chỉ flash bắt đầu ghi.
 * @param md5 Chuỗi MD5 (32 ký tự) để xác thực sau khi ghi.
 * @return ESP_OK nếu ghi và xác thực thành công.
 */
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
	// Gửi lệnh báo cho target biết sắp ghi phân vùng tại offset, kích thước total_size, và kích thước buffer
	esp_loader_error_t err = esp_loader_flash_start(offset, total_size, BUFFER_SIZE);
	if (err != ESP_LOADER_SUCCESS) {
		ESP_LOGE(TAG, "Failed to start flash for segment. err=%d", err);
		fwFile.close();
		return ESP_FAIL;
	}

	// Cấp phát bộ đệm để đọc dữ liệu từ SD và truyền qua UART
	uint8_t* buffer = (uint8_t*) malloc(BUFFER_SIZE);
	if (!buffer) {
		ESP_LOGE(TAG, "Failed to allocate buffer!");
		fwFile.close();
		return ESP_ERR_NO_MEM;
	}

	size_t bytes_written = 0;
	size_t bytes_read = 0;

	// Đọc từng khối dữ liệu từ file và ghi vào flash của Target
	while ((bytes_read = fwFile.read(buffer, BUFFER_SIZE)) > 0) {
		// Ghi khối dữ liệu vào flash
		err = esp_loader_flash_write(buffer, bytes_read);
		if (err != ESP_LOADER_SUCCESS) {
			ESP_LOGE(TAG, "Write error at offset %zu (err=%d)", bytes_written, err);
			free(buffer);
			fwFile.close();
			return ESP_FAIL;
		}

		bytes_written += bytes_read;
		// Tính toán và hiển thị tiến trình nạp
		uint32_t progress = (uint32_t)((bytes_written * 100) / total_size);

		if (progress % 25 == 0) {
			ESP_LOGI(TAG, "Progress: %" PRIu32 "%%", progress);
			// Cập nhật thông báo lên màn hình OLED
			oled_show_message(file_path.c_str(), (String("Progress: ") + String(progress) + String("%")).c_str());
		}
	}

	free(buffer);
	fwFile.close();

	ESP_LOGI(TAG, "Segment written %zu / %zu bytes OK", bytes_written, total_size);

	// --- KIỂM TRA MD5 (NẾU CÓ) ---
	if (md5.length() == 32) {
		char md5_ascii[33];
		// Sao chép chuỗi MD5 vào buffer và đảm bảo kết thúc bằng null
		strncpy(md5_ascii, md5.c_str(), 32);
		md5_ascii[32] = '\0';

		// Gửi lệnh MD5 để target tính toán và xác thực dữ liệu vừa ghi
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

/**
 * @brief Bắt đầu toàn bộ phiên nạp firmware: kết nối, nạp 3 phân vùng, và reset.
 * @param fw_id ID của firmware (dùng để tìm metadata file).
 * @return ESP_OK nếu toàn bộ quá trình thành công.
 */
esp_err_t flasher_begin_session(const std::string& fw_id)
{
	firmware_metadata_t metadata;

	// --- BƯỚC 1: LẤY THÔNG TIN FILE TỪ SD CARD ---
	// Lấy đường dẫn và MD5 của bootloader, partition table và app
	esp_err_t ret = sd_get_firmware_path(fw_id, metadata);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get firmware metadata for fw_id: %s", fw_id.c_str());
		return ret;
	}

	if (try_connect() != ESP_OK) {
		ESP_LOGE(TAG, "Failed to connect to target.");
		oled_show_message("Flash Session", "connect failed.");
		vTaskDelay(pdMS_TO_TICKS(500));
		return ESP_FAIL;
	} else {
		ESP_LOGI(TAG, "Connected to target successfully.");
		oled_show_message("Flash Session", "connected.");
	}

	// --- BƯỚC 3: BOOST BAUDRATE ---
	uint32_t new_baud = 921600;
	// Gửi lệnh thay đổi baudrate đến target
	esp_loader_error_t err_baud = esp_loader_change_transmission_rate(new_baud);
	if (err_baud == ESP_LOADER_SUCCESS) {
		// Nếu target chấp nhận, host (ESP32-C3) cũng phải thay đổi baudrate UART
		uart_set_baudrate(UART_NUM_1, new_baud);
		ESP_LOGI(TAG, "Baudrate boosted to %lu", new_baud);
	} else {
		ESP_LOGW(TAG, "Baudrate boost failed (err=%d), continue at 115200", err_baud);
	}


	// --- BƯỚC 4: GHI TỪNG PHÂN VÙNG THEO THỨ TỰ ---

	// 4a. Nạp bootloader (Offset: 0x1000)
	ret = flasher_write_segment(metadata.path_bootloader, ESP_BOOTLOADER_ADDR, metadata.md5_bootloader);
	if (ret != ESP_OK) return ret;

	// 4b. Nạp partition table (Offset: 0x8000)
	ret = flasher_write_segment(metadata.path_partition, ESP_PARTITION_ADDR, metadata.md5_partition);
	if (ret != ESP_OK) return ret;

	// 4c. Nạp firmware chính (Offset: 0x10000)
	ret = flasher_write_segment(metadata.path, ESP_APPLICATION_ADDR, metadata.md5);
	if (ret != ESP_OK) return ret;

	// --- BƯỚC 5: RESET TARGET VỀ CHẾ ĐỘ THƯỜNG ---
	esp_loader_reset_target();
	ESP_LOGI(TAG, "Resetting target to run app...");
	vTaskDelay(pdMS_TO_TICKS(200));

	ESP_LOGI(TAG, "Target restarted in normal mode.");
	ESP_LOGI(TAG, "Full firmware update completed successfully!");
	// sd_unmount(); // Giải phóng thẻ SD sau khi nạp xong

	return ESP_OK;
}

/**
 * @brief Thực hiện reset sequence với timing profile cụ thể
 * @param config Cấu hình chân GPIO
 * @param profile Timing profile
 * @return ESP_OK nếu thành công
 */
static esp_err_t reset_sequence_with_profile(const loader_esp32_config_t *config, const timing_profile_t *profile)
{
	// Cấu hình các chân điều khiển (EN và GPIO0) là OUTPUT
	gpio_set_direction((gpio_num_t)config->gpio0_trigger_pin, GPIO_MODE_OUTPUT);
	gpio_set_direction((gpio_num_t)config->reset_trigger_pin, GPIO_MODE_OUTPUT);

	ESP_LOGI(TAG, "Reset [%s]: hold=%lums, boot=%lums",
	         profile->name, profile->reset_hold_ms, profile->boot_hold_ms);

	// 1. Kéo GPIO0 xuống LOW (chuẩn bị vào download mode)
	gpio_set_level((gpio_num_t)config->gpio0_trigger_pin, 1);
	gpio_set_level((gpio_num_t)config->reset_trigger_pin, 0);
	vTaskDelay(pdMS_TO_TICKS(profile->reset_hold_ms));

	gpio_set_level((gpio_num_t)config->gpio0_trigger_pin, 0);
	gpio_set_level((gpio_num_t)config->reset_trigger_pin, 1);
	vTaskDelay(pdMS_TO_TICKS(profile->boot_hold_ms));

	return ESP_OK;
}

/**
 * @brief Xóa toàn bộ flash của chip Target.
 * @return ESP_OK nếu xóa thành công.
 */
esp_err_t flasher_chip_erase() {
	ESP_LOGI(TAG, "--- START CHIP ERASE ---");

	// 1. Handshake với Target (tự động thử các timing profiles)
	if (try_connect() != ESP_OK) {
		ESP_LOGE(TAG, "Failed to connect to target for erase.");
		oled_show_message("Erasing Chip", "failed to connect.");
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "Connected. Erasing chip (please wait)...");
	oled_show_message("Erasing Chip", "connected.");

	// 2. Gọi lệnh xóa toàn bộ (Hàm này sẽ BLOCK cho đến khi xóa xong, có thể mất vài giây)
	oled_show_message("Erasing Chip", "erasing...");
	esp_loader_error_t err = esp_loader_flash_erase();
	if (err != ESP_LOADER_SUCCESS) {
		ESP_LOGE(TAG, "Chip erase failed with error: %d", err);
		oled_show_message("Erasing Chip", "erase failed!");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Chip erase completed successfully!");
	oled_show_message("Erasing Chip", "SUCCESS!");

	// 3. Reset target lại để target chạy lại app (hoặc chỉ chạy bootloader nếu chưa có app)
	esp_loader_reset_target();

	return ESP_OK;
}

/**
 * @brief Hiển thị thông báo và khởi động lại ESP32 Host.
 */
void host_system_restart() {
	ESP_LOGI(TAG, "Dang khoi dong lai he thong...");

	// Hiệu ứng dấu chấm động: Restarting. -> Restarting.. -> Restarting...
	for (int i = 1; i <= 3; i++) {
		std::string dots(i, '.'); // Tạo chuỗi chứa i dấu chấm
		oled_show_message("Restarting", dots.c_str());
		vTaskDelay(pdMS_TO_TICKS(200));
	}

	// Gọi hàm khởi động lại hệ thống ESP-IDF
	esp_restart();
}

static esp_err_t try_connect(void){
	// --- BƯỚC 2: HANDSHAKE (ĐƯA TARGET VÀO BOOTLOADER) ---
	esp_loader_connect_args_t connect_config = ESP_LOADER_CONNECT_DEFAULT();
	
	const timing_profile_t *profile = &TIMING_PROFILES[0];
	ESP_LOGI(TAG, "Connecting to target using profile: %s", profile->name);

	// Reset vào bootloader
	reset_sequence_with_profile(&config, profile);

	// Flush UART - cả RX và TX để đảm bảo buffer sạch
	uart_flush(config.uart_port);          // Flush TX
	uart_flush_input(config.uart_port);    // Flush RX
	vTaskDelay(pdMS_TO_TICKS(50));         // Chờ ổn định

	// Thử kết nối
	if (esp_loader_connect(&connect_config) == ESP_LOADER_SUCCESS) {
		ESP_LOGI(TAG, "SUCCESS! Connected with profile: %s", profile->name);
		return ESP_OK;
	} else {
		ESP_LOGE(TAG, "Failed to connect to target.");
		oled_show_message("Flash Session", "connect failed.");
		vTaskDelay(pdMS_TO_TICKS(500));
		return ESP_FAIL;
	}
}