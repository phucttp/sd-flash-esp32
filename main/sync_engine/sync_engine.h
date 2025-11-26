#ifndef SYNC_ENGINE_H
#define SYNC_ENGINE_H

/**
 * @brief Hàm khởi chạy quy trình đồng bộ Firmware.
 * Chức năng:
 * 1. Tải Index từ Server.
 * 2. So sánh với Index trên thẻ nhớ (Local).
 * 3. Tải file mới/file thay đổi (tự động giải mã).
 * 4. Xóa file thừa.
 * 5. Cập nhật lại Index trên thẻ.
 */
void sync_engine_run(bool force_clean);

#endif // SYNC_ENGINE_H