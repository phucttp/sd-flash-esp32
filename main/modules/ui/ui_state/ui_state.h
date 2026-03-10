/**
 * @file ui_state.h
 * @brief Quản lý trạng thái UI (tab + item) để restore sau khi restart
 *
 * Module này lưu trạng thái UI hiện tại vào SD card trước khi restart,
 * sau đó restore lại khi khởi động. Giúp UX mượt mà hơn, user không
 * bị "văng" về tab đầu sau mỗi lần flash firmware.
 *
 * File format: /config/ui_state.txt (plain text)
 * Format: tab_index,item_id,timestamp
 * Example: 0,FW_ESP32_001,1676543210
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Độ dài tối đa của item_id (firmware ID hoặc menu item name)
#define UI_STATE_ITEM_ID_MAX_LEN 32

// Timeout: state cũ hơn 10 phút sẽ bị ignore (tránh restore state từ session trước)
#define UI_STATE_TIMEOUT_SEC (10 * 60)

/**
 * @brief Lưu trạng thái UI hiện tại vào SD card
 *
 * Ghi file /config/ui_state.txt với format: tab,item_id,timestamp
 * File này sẽ được đọc lại khi khởi động để restore UI state.
 *
 * @param tab_index    Index của tab hiện tại (0-3: FW/Tools/Settings/About)
 * @param item_id      ID của item đang chọn (firmware ID hoặc "NONE" nếu không có)
 *
 * @return
 * - ESP_OK: Lưu thành công
 * - ESP_FAIL: Lỗi ghi file (SD card không có, full, v.v.)
 * - ESP_ERR_INVALID_ARG: item_id quá dài (>32 chars)
 *
 * @note
 * - Hàm này GHI ĐÈ file cũ nếu tồn tại
 * - Timestamp tự động lấy từ esp_timer_get_time()
 * - Cần gọi TRƯỚC esp_restart() để đảm bảo file được flush
 */
esp_err_t ui_state_save(int tab_index, const char* item_id);

/**
 * @brief Đọc trạng thái UI từ SD card
 *
 * Đọc file /config/ui_state.txt và parse ra tab_index + item_id.
 * Validate timestamp: nếu quá cũ (>10 phút) → ignore + delete file.
 *
 * @param[out] tab_index     Pointer để nhận tab index (0-3)
 * @param[out] item_id       Buffer để nhận item ID
 * @param[in]  item_id_len   Kích thước buffer item_id (phải ≥ UI_STATE_ITEM_ID_MAX_LEN)
 *
 * @return
 * - ESP_OK: Đọc thành công, state hợp lệ
 * - ESP_ERR_NOT_FOUND: File không tồn tại (chưa có state hoặc đã bị xóa)
 * - ESP_ERR_INVALID_STATE: File tồn tại nhưng timeout hoặc format sai
 * - ESP_FAIL: Lỗi đọc file
 *
 * @note
 * - Hàm này KHÔNG XÓA file sau khi đọc → cần gọi ui_state_clear() sau restore
 * - Nếu return ESP_ERR_INVALID_STATE → file đã bị auto-delete
 */
esp_err_t ui_state_load(int* tab_index, char* item_id, size_t item_id_len);

/**
 * @brief Xóa file state (sau khi restore thành công)
 *
 * Xóa /config/ui_state.txt để tránh restore lại lần sau.
 * State chỉ restore 1 lần duy nhất sau mỗi restart.
 *
 * @return ESP_OK nếu xóa thành công hoặc file không tồn tại
 */
esp_err_t ui_state_clear(void);

/**
 * @brief Kiểm tra xem có state file hay không
 *
 * @return true nếu /config/ui_state.txt tồn tại, false nếu không
 */
bool ui_state_exists(void);

#ifdef __cplusplus
}
#endif
