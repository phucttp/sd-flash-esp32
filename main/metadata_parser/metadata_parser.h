/**
 * @file metadata_parser.h
 * @brief Module chuyên trách xử lý phân tích cú pháp (Parsing) dữ liệu JSON.
 * @details Nhận chuỗi JSON thô từ Server -> Chuyển thành cấu trúc Map C++ để code dễ xử lý.
 */

#ifndef METADATA_PARSER_H
#define METADATA_PARSER_H

#include <Arduino.h>
#include "../firmware_types.h" // Include file chứa struct firmware_metadata_t

/**
 * @brief Phân tích chuỗi JSON danh sách firmware thành bản đồ dữ liệu (Map).
 * * @param json_content Chuỗi JSON thô tải từ Server (index.txt).
 * @param out_map      Biến tham chiếu (Reference) để chứa kết quả sau khi parse.
 * Hàm sẽ xóa sạch dữ liệu cũ trong map này trước khi nạp mới.
 * * @return true  Nếu parse thành công (JSON hợp lệ).
 * @return false Nếu JSON lỗi hoặc không đủ bộ nhớ.
 */
bool metadata_parse_json(const String& json_content, FirmwareMap& out_map);

#endif // METADATA_PARSER_H