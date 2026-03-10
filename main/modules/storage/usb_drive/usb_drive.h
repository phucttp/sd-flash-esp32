/**
 * @file usb_drive.h
 * @brief USB Mass Storage (MSC) + CDC virtual COM port module
 *
 * Chức năng:
 *   - Mount phân vùng FAT nội bộ (~12.9MB) làm ổ đĩa USB cho PC
 *   - Cung cấp cổng COM ảo (CDC) để debug qua USB
 *   - Lock/unlock write từ PC khi ESP32 đang flash target
 *
 * Mount point VFS: /usb
 * Partition label:  fatfs
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Mount point VFS — tất cả file access dùng prefix này
#define USB_DRIVE_MOUNT "/usb"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo USB drive: mount FFat + bật TinyUSB MSC + CDC
 *
 * Gọi một lần khi boot, trước khi đọc firmware metadata.
 * Tự động format nếu là lần chạy đầu tiên (phân vùng trống).
 *
 * @return ESP_OK nếu thành công
 */
esp_err_t usb_drive_init(void);

/**
 * @brief Lock MSC write — gọi trước khi bắt đầu flash target
 *
 * PC vẫn đọc được nhưng không ghi được.
 * Tránh race condition khi ESP32 đang đọc file firmware.
 */
void usb_drive_lock(void);

/**
 * @brief Unlock MSC write — gọi sau khi flash target xong
 */
void usb_drive_unlock(void);

/**
 * @brief Kiểm tra USB drive đã sẵn sàng chưa (mounted thành công)
 * @return true nếu FAT filesystem đã mount
 */
bool usb_drive_is_ready(void);

/**
 * @brief Trạng thái kết nối USB (debug)
 * @param[out] connected  true nếu USB cable connected (VBUS detected)
 * @param[out] mounted    true nếu host đã SetConfiguration
 */
void usb_drive_get_status(bool *connected, bool *mounted);

#ifdef __cplusplus
}
#endif
