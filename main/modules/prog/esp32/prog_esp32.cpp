/**
 * @file prog_esp32.cpp
 * @brief Engine nạp firmware cho mục tiêu ESP32 qua giao tiếp UART.
 *
 * Chức năng chính:
 *   - Thiết lập kết nối với bootloader ROM của ESP32 target (handshake qua esp_loader)
 *   - Nạp tuần tự 3 phân vùng: bootloader, partition table, application
 *   - Xác thực từng phân vùng sau nạp bằng MD5 checksum
 *   - Hỗ trợ xóa toàn bộ chip (chip erase) độc lập với quá trình nạp
 */

// ============================================================
// 1. STANDARD & ESP-IDF LIBRARIES
// ============================================================
#include "prog_esp32.h"

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/gpio.h"

// ============================================================
// 2. ESP-SERIAL-FLASHER LIBRARIES
// ============================================================
#include "esp32_port.h"     // Cấu hình cổng UART và GPIO cho esp_loader
#include "esp_loader.h"     // Các hàm API cấp cao của bootloader ROM

// ============================================================
// 3. PROJECT LIBRARIES
// ============================================================
#include "Arduino.h"
#include "tft_ui.h"         // TftUI Library
#include "../../storage/usb_drive/usb_drive.h"
#include "mbedtls/md5.h"

// Extern UI instance from main.cpp
extern TftUI ui;

// TAG dùng để lọc log cho module này
static const char *TAG = "FLASHER";

// RAM-only saved combo (không ghi flash — hạn chế wear)
static int g_saved_combo = -1;

/**
 * @brief Khởi tạo phần cứng của HOST (ESP32) để giao tiếp với TARGET.
 * CHẠY 1 LẦN DUY NHẤT lúc khởi động.
 */
const loader_esp32_config_t config = {
	.baud_rate = 115200,
	.uart_port = UART_NUM_1,
	.uart_rx_pin = FLASH_UART_RX_PIN,
	.uart_tx_pin = FLASH_UART_TX_PIN,
	.reset_trigger_pin = FLASH_RESET_PIN,
	.gpio0_trigger_pin = FLASH_BOOT_PIN,
};

// Khai báo hàm
static esp_err_t try_connect(void);

// Flag để tránh init UART nhiều lần
static bool g_flasher_initialized = false;

/**
 * @brief Khởi tạo giao tiếp UART và cổng nạp.
 * @return ESP_OK nếu thành công, ESP_FAIL nếu thất bại.
 */
esp_err_t flasher_init() {
	if (g_flasher_initialized) {
		ESP_LOGI(TAG, "Flasher already initialized, skipping...");
		return ESP_OK;
	}

	ESP_LOGI(TAG, "Initializing UART connection for flasher...");
	if (loader_port_esp32_init(&config) != ESP_LOADER_SUCCESS) {
		ESP_LOGE(TAG, "serial initialization failed.");
		return ESP_FAIL;
	}

	g_flasher_initialized = true;
	ESP_LOGI(TAG, "UART connection initialized at baud rate 115200");
	return ESP_OK;
}

/**
 * @brief Tính MD5 checksum của file trên USB drive.
 * @param file_path Đường dẫn relative (ví dụ "/ES_MyFW/app.bin").
 * @return Chuỗi hex 32 ký tự, hoặc rỗng nếu lỗi.
 */
static std::string compute_file_md5(const std::string& file_path) {
	std::string full_path = std::string(USB_DRIVE_MOUNT) + file_path;
	FILE* f = fopen(full_path.c_str(), "rb");
	if (!f) {
		ESP_LOGE(TAG, "MD5: cannot open %s", full_path.c_str());
		return "";
	}

	mbedtls_md5_context ctx;
	mbedtls_md5_init(&ctx);
	mbedtls_md5_starts(&ctx);

	uint8_t buf[1024];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		mbedtls_md5_update(&ctx, buf, n);
	}
	fclose(f);

	uint8_t digest[16];
	mbedtls_md5_finish(&ctx, digest);
	mbedtls_md5_free(&ctx);

	char hex[33];
	for (int i = 0; i < 16; i++) {
		snprintf(hex + i * 2, 3, "%02x", digest[i]);
	}
	return std::string(hex);
}

/**
 * @brief Ghi một phân vùng (segment) của firmware lên chip Target.
 * @param file_path Đường dẫn file binary (relative, ví dụ "/ES_MyFW/app.bin").
 * @param offset Địa chỉ flash bắt đầu ghi.
 * @param md5 Chuỗi MD5 (32 ký tự) để xác thực sau khi ghi. Nếu rỗng, tự tính từ file.
 * @return ESP_OK nếu ghi và xác thực thành công.
 */
esp_err_t flasher_write_segment(const std::string& file_path, uint32_t offset, const std::string& md5)
{
	ESP_LOGI(TAG, "==== Writing segment ====");
	ESP_LOGI(TAG, "File: %s | Offset: 0x%08" PRIx32, file_path.c_str(), offset);

	// --- MỞ FILE qua VFS POSIX ---
	std::string full_path = std::string(USB_DRIVE_MOUNT) + file_path;
	FILE* fwFile = fopen(full_path.c_str(), "rb");
	if (!fwFile) {
		ESP_LOGE(TAG, "Failed to open file: %s", full_path.c_str());
		return ESP_ERR_NOT_FOUND;
	}

	struct stat st;
	stat(full_path.c_str(), &st);
	size_t total_size = (size_t)st.st_size;
	if (total_size == 0) {
		ESP_LOGE(TAG, "File is empty: %s", full_path.c_str());
		fclose(fwFile);
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Segment size: %zu bytes", total_size);

	// --- BẮT ĐẦU GHI FLASH ---
	esp_loader_error_t err = esp_loader_flash_start(offset, total_size, FLASHER_BUFFER_SIZE);
	if (err != ESP_LOADER_SUCCESS) {
		ESP_LOGE(TAG, "Failed to start flash for segment. err=%d", err);
		fclose(fwFile);
		return ESP_FAIL;
	}

	uint8_t* buffer = (uint8_t*) malloc(FLASHER_BUFFER_SIZE);
	if (!buffer) {
		ESP_LOGE(TAG, "Failed to allocate buffer!");
		fclose(fwFile);
		return ESP_ERR_NO_MEM;
	}

	size_t bytes_written = 0;
	size_t bytes_read = 0;

	while ((bytes_read = fread(buffer, 1, FLASHER_BUFFER_SIZE, fwFile)) > 0) {
		err = esp_loader_flash_write(buffer, bytes_read);
		if (err != ESP_LOADER_SUCCESS) {
			ESP_LOGE(TAG, "Write error at offset %zu (err=%d)", bytes_written, err);
			free(buffer);
			fclose(fwFile);
			return ESP_FAIL;
		}

		bytes_written += bytes_read;
		uint32_t progress = (uint32_t)((bytes_written * 100) / total_size);

		static uint32_t last_progress = 0;
		if (progress != last_progress && progress % 10 == 0) {
			last_progress = progress;
			ESP_LOGI(TAG, "Progress: %" PRIu32 "%%", progress);
			ui.showProgress(file_path.c_str(), (int)progress);
		}
	}

	free(buffer);
	fclose(fwFile);

	ESP_LOGI(TAG, "Segment written %zu / %zu bytes OK", bytes_written, total_size);

	// --- KIỂM TRA MD5 — tự tính nếu metadata không cung cấp ---
	std::string verify_md5 = md5;
	if (verify_md5.length() != 32) {
		ESP_LOGI(TAG, "No MD5 in metadata, computing from file...");
		verify_md5 = compute_file_md5(file_path);
	}

	if (verify_md5.length() == 32) {
		char md5_ascii[33];
		strncpy(md5_ascii, verify_md5.c_str(), 32);
		md5_ascii[32] = '\0';

		ESP_LOGI(TAG, "Verifying MD5: %s", md5_ascii);
		err = esp_loader_flash_verify_known_md5(offset, total_size, (const uint8_t*) md5_ascii);
		if (err != ESP_LOADER_SUCCESS) {
			ESP_LOGE(TAG, "MD5 check failed for segment!");
			return ESP_FAIL;
		}
		ESP_LOGI(TAG, "MD5 verified OK for segment!");
	} else {
		ESP_LOGW(TAG, "Cannot compute MD5, skipping verification.");
	}

	ESP_LOGI(TAG, "Segment at 0x%08" PRIx32 " written successfully.", offset);
	return ESP_OK;
}

/**
 * @brief Bắt đầu toàn bộ phiên nạp firmware: kết nối, nạp 3 phân vùng, và reset.
 * @param fw_id ID của firmware (tên folder trên USB drive).
 * @return ESP_OK nếu toàn bộ quá trình thành công.
 */
esp_err_t flasher_begin_session(const std::string& fw_id)
{
	firmware_metadata_t metadata;

	// --- BƯỚC 1: LẤY THÔNG TIN FILE ---
	esp_err_t ret = sd_get_firmware_path(fw_id, metadata);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get firmware metadata for fw_id: %s", fw_id.c_str());
		return ret;
	}

	if (try_connect() != ESP_OK) {
		ESP_LOGE(TAG, "Failed to connect to target.");
		ui.showMessage("Flash Session", "connect failed.");
		vTaskDelay(pdMS_TO_TICKS(500));
		return ESP_FAIL;
	} else {
		ESP_LOGI(TAG, "Connected to target successfully.");
		ui.showMessage("Flash Session", "connected.");
	}

	// --- BƯỚC 3: BOOST BAUDRATE ---
	uint32_t new_baud = 921600;
	esp_loader_error_t err_baud = esp_loader_change_transmission_rate(new_baud);
	if (err_baud == ESP_LOADER_SUCCESS) {
		uart_set_baudrate(UART_NUM_1, new_baud);
		ESP_LOGI(TAG, "Baudrate boosted to %lu", new_baud);
	} else {
		ESP_LOGW(TAG, "Baudrate boost failed (err=%d), continue at 115200", err_baud);
	}

	// --- BƯỚC 4: GHI TỪNG PHÂN VÙNG THEO THỨ TỰ ---
	ESP_LOGI(TAG, ">>> PLAIN FLASH MODE <<<");

	// 4a. Nạp bootloader (Offset: 0x1000) — skip nếu không có
	if (!metadata.path_bootloader.empty()) {
		ret = flasher_write_segment(metadata.path_bootloader, ESP_BOOTLOADER_ADDR, metadata.md5_bootloader);
		if (ret != ESP_OK) return ret;
	} else {
		ESP_LOGI(TAG, "No bootloader — skipping (app-only flash)");
	}

	// 4b. Nạp partition table (Offset: 0x8000) — skip nếu không có
	if (!metadata.path_partition.empty()) {
		ret = flasher_write_segment(metadata.path_partition, ESP_PARTITION_ADDR, metadata.md5_partition);
		if (ret != ESP_OK) return ret;
	} else {
		ESP_LOGI(TAG, "No partition table — skipping (app-only flash)");
	}

	// 4c. Nạp firmware chính (Offset: 0x10000) — bắt buộc
	ret = flasher_write_segment(metadata.path, ESP_APPLICATION_ADDR, metadata.md5);
	if (ret != ESP_OK) return ret;

	// --- BƯỚC 5: RESET TARGET + RESTORE BAUD ---
	esp_loader_reset_target();
	uart_set_baudrate(UART_NUM_1, config.baud_rate);
	ESP_LOGI(TAG, "Resetting target, baud restored to %lu", (unsigned long)config.baud_rate);
	vTaskDelay(pdMS_TO_TICKS(200));

	ESP_LOGI(TAG, "Full firmware update completed successfully!");
	return ESP_OK;
}


/**
 * @brief Reset sequence kiểu esptool - brute force thử tất cả logic combinations
 */
static void do_reset_sequence(const loader_esp32_config_t *config, int reset_invert, int boot_invert, uint32_t reset_ms, uint32_t boot_ms)
{
	int reset_active = reset_invert ? 1 : 0;
	int reset_idle = reset_invert ? 0 : 1;
	int boot_active = boot_invert ? 1 : 0;
	int boot_idle = boot_invert ? 0 : 1;

	gpio_set_level((gpio_num_t)config->gpio0_trigger_pin, boot_active);
	vTaskDelay(pdMS_TO_TICKS(50));

	gpio_set_level((gpio_num_t)config->reset_trigger_pin, reset_active);
	vTaskDelay(pdMS_TO_TICKS(reset_ms));
	gpio_set_level((gpio_num_t)config->reset_trigger_pin, reset_idle);

	vTaskDelay(pdMS_TO_TICKS(boot_ms));

	gpio_set_level((gpio_num_t)config->gpio0_trigger_pin, boot_idle);
}

/**
 * @brief Brute force try tất cả GPIO logic combinations như esptool
 * @return ESP_OK nếu kết nối thành công
 */
static esp_err_t try_all_reset_combinations(const loader_esp32_config_t *config)
{
	gpio_set_direction((gpio_num_t)config->gpio0_trigger_pin, GPIO_MODE_OUTPUT);
	gpio_set_direction((gpio_num_t)config->reset_trigger_pin, GPIO_MODE_OUTPUT);

	esp_loader_connect_args_t connect_config = ESP_LOADER_CONNECT_DEFAULT();

	// 4 combinations: (reset_invert, boot_invert)
	const int combinations[4][2] = {
		{1, 1},  // Both transistor (most common for autoboot boards)
		{1, 0},  // Reset transistor, Boot direct
		{0, 1},  // Reset direct, Boot transistor
		{0, 0},  // Both direct
	};
	const char* combo_names[4] = {
		"RST=inv, BOOT=inv",
		"RST=inv, BOOT=dir",
		"RST=dir, BOOT=inv",
		"RST=dir, BOOT=dir",
	};

	// Timing variations
	const uint32_t timings[3][2] = {
		{100, 50},   // Fast
		{100, 100},  // Normal
		{200, 200},  // Slow
	};

	// Animation: spinning chars + attempt counter
	const char spinner[] = "|/-\\";
	int attempt = 0;
	const int totalAttempts = 12;  // 3 timings x 4 combos

	for (int t = 0; t < 3; t++) {
		for (int c = 0; c < 4; c++) {
			attempt++;

			// [ANIMATION] Hiển thị tiến trình kết nối
			char line1[22];
			char line2[22];
			snprintf(line1, sizeof(line1), "Connecting %c", spinner[attempt % 4]);
			snprintf(line2, sizeof(line2), "Try %d/%d...", attempt, totalAttempts);
			ui.showMessage(line1, line2);

			ESP_LOGI(TAG, "Trying [%d/%d]: %s, timing=%lu/%lu ms",
			         attempt, totalAttempts, combo_names[c], timings[t][0], timings[t][1]);

			// Release all first
			gpio_set_level((gpio_num_t)config->gpio0_trigger_pin, combinations[c][1] ? 0 : 1);
			gpio_set_level((gpio_num_t)config->reset_trigger_pin, combinations[c][0] ? 0 : 1);
			vTaskDelay(pdMS_TO_TICKS(100));

			// Do reset sequence
			do_reset_sequence(config, combinations[c][0], combinations[c][1],
			                  timings[t][0], timings[t][1]);

			// Flush UART
			uart_flush(config->uart_port);
			uart_flush_input(config->uart_port);
			vTaskDelay(pdMS_TO_TICKS(50));

			// Try connect
			int combo_index = t * 4 + c;
			if (esp_loader_connect(&connect_config) == ESP_LOADER_SUCCESS) {
				ESP_LOGI(TAG, "SUCCESS! combo #%d: %s, timing=%lu/%lu",
				         combo_index, combo_names[c], timings[t][0], timings[t][1]);
				g_saved_combo = combo_index;
				ui.showMessage("Connected!", "Success");
				return ESP_OK;
			}

			ESP_LOGW(TAG, "Failed, trying next...");
			vTaskDelay(pdMS_TO_TICKS(100));
		}
	}

	ESP_LOGE(TAG, "All combinations failed!");
	return ESP_FAIL;
}

/**
 * @brief Xóa toàn bộ flash của chip Target.
 * @return ESP_OK nếu xóa thành công.
 */
esp_err_t flasher_chip_erase() {
	ESP_LOGI(TAG, "--- START CHIP ERASE ---");

	if (try_connect() != ESP_OK) {
		ESP_LOGE(TAG, "Failed to connect to target for erase.");
		ui.showMessage("Erasing Chip", "failed to connect.");
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "Connected. Erasing chip (please wait)...");
	ui.showMessage("Erasing Chip", "connected.");

	ui.showMessage("Erasing Chip", "erasing...");
	esp_loader_error_t err = esp_loader_flash_erase();
	if (err != ESP_LOADER_SUCCESS) {
		ESP_LOGE(TAG, "Chip erase failed with error: %d", err);
		ui.showMessage("Erasing Chip", "erase failed!");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Chip erase completed successfully!");
	ui.showMessage("Erasing Chip", "SUCCESS!");

	esp_loader_reset_target();
	uart_set_baudrate(UART_NUM_1, config.baud_rate);
	return ESP_OK;
}

// ============================================================
// SCAN BOOT API - RAM-only combo cache
// ============================================================

int flasher_load_saved_combo(void) {
	return g_saved_combo;
}

void flasher_clear_saved_combo(void) {
	g_saved_combo = -1;
}

/**
 * @brief Thử kết nối với combo cụ thể
 */
static esp_err_t try_connect_with_combo(int combo_index) {
	const int combinations[4][2] = {
		{1, 1}, {1, 0}, {0, 1}, {0, 0}
	};
	const uint32_t timings[3][2] = {
		{100, 50},   // Fast  — must match try_all_reset_combinations
		{100, 100},  // Normal
		{200, 200},  // Slow
	};

	int t = combo_index / 4;
	int c = combo_index % 4;

	gpio_set_direction((gpio_num_t)config.gpio0_trigger_pin, GPIO_MODE_OUTPUT);
	gpio_set_direction((gpio_num_t)config.reset_trigger_pin, GPIO_MODE_OUTPUT);

	esp_loader_connect_args_t connect_config = ESP_LOADER_CONNECT_DEFAULT();

	// Retry saved combo up to 3 times
	for (int retry = 0; retry < 3; retry++) {
		ESP_LOGI(TAG, "Trying saved combo #%d (timing=%lu/%lu) attempt %d/3",
		         combo_index, timings[t][0], timings[t][1], retry + 1);

		gpio_set_level((gpio_num_t)config.gpio0_trigger_pin, combinations[c][1] ? 0 : 1);
		gpio_set_level((gpio_num_t)config.reset_trigger_pin, combinations[c][0] ? 0 : 1);
		vTaskDelay(pdMS_TO_TICKS(100));

		do_reset_sequence(&config, combinations[c][0], combinations[c][1],
		                  timings[t][0], timings[t][1]);

		uart_flush(config.uart_port);
		uart_flush_input(config.uart_port);
		vTaskDelay(pdMS_TO_TICKS(50));

		if (esp_loader_connect(&connect_config) == ESP_LOADER_SUCCESS) {
			ESP_LOGI(TAG, "Connected with saved combo #%d!", combo_index);
			return ESP_OK;
		}

		ESP_LOGW(TAG, "Saved combo retry %d/3 failed", retry + 1);
	}

	return ESP_FAIL;
}

/**
 * @brief Scan tất cả combo và lưu combo working
 */
int flasher_scan_and_save_combo(void) {
	gpio_set_direction((gpio_num_t)config.gpio0_trigger_pin, GPIO_MODE_OUTPUT);
	gpio_set_direction((gpio_num_t)config.reset_trigger_pin, GPIO_MODE_OUTPUT);

	// trials=7 → 700ms sync window/combo
	esp_loader_connect_args_t connect_config = {
		.sync_timeout = 100,
		.trials = 7,
	};

	const int combinations[4][2] = {
		{1, 1}, {1, 0}, {0, 1}, {0, 0}
	};
	const char* combo_names[4] = {
		"RST=inv,BOOT=inv",
		"RST=inv,BOOT=dir",
		"RST=dir,BOOT=inv",
		"RST=dir,BOOT=dir"
	};
	const uint32_t timings[3][2] = {
		{100, 100}, {200, 200}, {300, 300}
	};

	const char spinner[] = "|/-\\";
	int attempt = 0;
	const int totalAttempts = 12;

	for (int t = 0; t < 3; t++) {
		for (int c = 0; c < 4; c++) {
			int combo_index = t * 4 + c;
			attempt++;

			char line1[22];
			char line2[22];
			snprintf(line1, sizeof(line1), "Scanning %c", spinner[attempt % 4]);
			snprintf(line2, sizeof(line2), "Try %d/%d...", attempt, totalAttempts);
			ui.showMessage(line1, line2);

			ESP_LOGI(TAG, "Scan [%d/%d]: %s, timing=%lu/%lu",
			         attempt, totalAttempts, combo_names[c], timings[t][0], timings[t][1]);

			gpio_set_level((gpio_num_t)config.gpio0_trigger_pin, combinations[c][1] ? 0 : 1);
			gpio_set_level((gpio_num_t)config.reset_trigger_pin, combinations[c][0] ? 0 : 1);
			vTaskDelay(pdMS_TO_TICKS(50));

			do_reset_sequence(&config, combinations[c][0], combinations[c][1],
			                  timings[t][0], timings[t][1]);

			uart_flush(config.uart_port);
			uart_flush_input(config.uart_port);
			vTaskDelay(pdMS_TO_TICKS(20));

			if (esp_loader_connect(&connect_config) == ESP_LOADER_SUCCESS) {
				ESP_LOGI(TAG, "FOUND! Combo #%d: %s, timing=%lu/%lu",
				         combo_index, combo_names[c], timings[t][0], timings[t][1]);

				g_saved_combo = combo_index;

				ui.showMessage("FOUND!", line2);
				vTaskDelay(pdMS_TO_TICKS(500));

				esp_loader_reset_target();

				return combo_index;
			}

			vTaskDelay(pdMS_TO_TICKS(50));
		}
	}

	ESP_LOGE(TAG, "Scan failed - no working combo found!");
	return -1;
}

static esp_err_t try_connect(void) {
	int saved = flasher_load_saved_combo();
	if (saved >= 0) {
		ESP_LOGI(TAG, "=== FAST CONNECT (saved combo #%d) ===", saved);
		ui.showMessage("Connecting...", "Using saved cfg");

		if (try_connect_with_combo(saved) == ESP_OK) {
			ui.showMessage("Connected!", "Fast mode");
			vTaskDelay(pdMS_TO_TICKS(300));
			return ESP_OK;
		}

		ESP_LOGW(TAG, "Saved combo failed, clearing and trying brute force...");
		flasher_clear_saved_combo();
	}

	ESP_LOGI(TAG, "=== BRUTE FORCE CONNECT (esptool style) ===");

	esp_err_t result = try_all_reset_combinations(&config);

	if (result == ESP_OK) {
		vTaskDelay(pdMS_TO_TICKS(300));
		return ESP_OK;
	} else {
		ESP_LOGE(TAG, "All reset combinations failed!");
		ui.showMessage("FAILED!", "Check wiring");
		vTaskDelay(pdMS_TO_TICKS(1500));
		return ESP_FAIL;
	}
}
