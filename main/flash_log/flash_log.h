/**
 * @file flash_log.h
 * @brief Module ghi log các thao tác flash/erase/scan trên USB drive.
 *
 * File log: /usb/flash_log.txt
 * Format:   #NNN | UP:HHhMMm | FW_ID | ACTION | RESULT | DETAIL
 * Giới hạn: 100 entries, auto-rotate (xóa cũ nhất khi đầy).
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Kết quả thao tác
#define LOG_RESULT_OK    "OK"
#define LOG_RESULT_FAIL  "FAIL"
#define LOG_RESULT_CANCEL "CANCEL"

// Loại thao tác
#define LOG_ACTION_FLASH_ESP   "FLASH_ESP"
#define LOG_ACTION_FLASH_SWD   "FLASH_SWD"
#define LOG_ACTION_ERASE_ESP   "ERASE_ESP"
#define LOG_ACTION_ERASE_SWD   "ERASE_SWD"
#define LOG_ACTION_SCAN_BOOT   "SCAN_BOOT"
#define LOG_ACTION_RDP_DISABLE "RDP_DISABLE"

/**
 * @brief Ghi 1 entry log vào file. Auto-rotate nếu vượt 100 entries.
 *
 * @param action   Loại thao tác (LOG_ACTION_*)
 * @param fw_id    ID firmware (hoặc NULL nếu không liên quan)
 * @param result   Kết quả (LOG_RESULT_*)
 * @param detail   Chi tiết thêm (e.g., "MD5:OK", "RDP_LOCKED", hoặc NULL)
 */
void flash_log_write(const char* action, const char* fw_id,
                     const char* result, const char* detail);

/**
 * @brief Đọc toàn bộ log file ra buffer (để hiển thị trên UI hoặc debug).
 * @param buf     Buffer output (caller allocate)
 * @param buf_len Kích thước buffer
 * @return Số bytes đã đọc, hoặc 0 nếu lỗi/file rỗng
 */
int flash_log_read(char* buf, int buf_len);

/**
 * @brief Lấy số entry hiện tại trong log file.
 * @return Số entries (0 nếu file chưa tồn tại)
 */
int flash_log_count(void);

/**
 * @brief Xóa toàn bộ log file.
 */
void flash_log_clear(void);

#ifdef __cplusplus
}
#endif
