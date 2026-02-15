/**
 * @file app_actions.h
 * @brief API cac hanh dong ung dung cap cao
 *
 * Module nay cung cap API sach cho tat ca cac hanh dong nguoi dung.
 * Lop UI goi cac ham nay ma khong can biet chi tiet implementation.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// CALLBACK TYPES
// ============================================================
typedef void (*action_progress_cb_t)(const char* text, int percent);
typedef bool (*action_confirm_cb_t)(const char* title, const char* message);
typedef void (*action_message_cb_t)(const char* title, const char* message);
typedef void (*action_spinner_cb_t)(const char* text, int frame);

// Monitor callback
#define APP_ACTIONS_LOG_LINES 7
#define APP_ACTIONS_LOG_LINE_WIDTH 21
typedef void (*monitor_draw_cb_t)(char log_buffer[APP_ACTIONS_LOG_LINES][APP_ACTIONS_LOG_LINE_WIDTH + 1],
                                   int log_write_index, bool has_activity);

// ============================================================
// CONFIG STRUCT - Gop tat ca callbacks
// ============================================================
typedef struct {
    int sd_cs_pin;
    int btn_ok_pin;
    int btn_up_pin;      // Them de check combo exit
    int btn_down_pin;    // UP + DOWN = cancel action
    action_progress_cb_t on_progress;
    action_confirm_cb_t on_confirm;
    action_message_cb_t on_message;
    action_spinner_cb_t on_spinner;
    monitor_draw_cb_t on_monitor_draw;
} app_actions_config_t;

// ============================================================
// KHOI TAO
// ============================================================

/**
 * @brief Khoi tao module voi config struct
 */
void app_actions_init(const app_actions_config_t* config);

// ============================================================
// ACTIONS API
// ============================================================

/**
 * @brief Dong bo firmware tu cloud (WiFi + Git Sync)
 * @return ESP_OK neu thanh cong
 */
esp_err_t action_sync_firmware(void);

/**
 * @brief Mo cong cau hinh WiFi/URL
 * @return ESP_OK neu thanh cong
 */
esp_err_t action_config_wifi(void);

/**
 * @brief Chay che do serial monitor
 * @note Ham nay block cho den khi nguoi dung thoat
 * @return ESP_OK khi nguoi dung thoat
 */
esp_err_t action_monitor(void);

/**
 * @brief Xoa toan bo chip target
 * @return ESP_OK neu thanh cong, ESP_FAIL neu loi
 */
esp_err_t action_erase_chip(void);

/**
 * @brief Quet va luu combo che do boot
 * @return ESP_OK neu tim thay combo, ESP_FAIL neu khong
 */
esp_err_t action_scan_boot(void);

/**
 * @brief Nap firmware vao thiet bi target
 * @param fw_id ID firmware tu metadata SD card
 * @return ESP_OK neu thanh cong, ESP_FAIL neu loi
 */
esp_err_t action_flash_firmware(const char* fw_id);

/**
 * @brief Xoa toan bo chip STM32 target qua SWD
 * @return ESP_OK neu thanh cong, ESP_FAIL neu loi
 */
esp_err_t action_erase_chip_swd(void);

// ============================================================
// STATUS API
// ============================================================

/**
 * @brief Lay trang thai hanh dong hien tai
 */
void app_actions_get_status(const char** text, int* progress, bool* is_busy);

#ifdef __cplusplus
}
#endif
