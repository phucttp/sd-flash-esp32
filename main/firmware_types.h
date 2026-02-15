// firmware_types.h
#pragma once
#include <string>
#include <map>

typedef struct {
    std::string device_type;
    std::string version;
    std::string description;  // Mô tả chi tiết firmware (hiển thị trên OLED Status tab)
    std::string path;
    std::string md5;
    std::string path_bootloader;
    std::string md5_bootloader;
    std::string path_partition;
    std::string md5_partition;
    bool encrypted = false;  // true if files are AES-128-CBC encrypted (.enc)
} firmware_metadata_t;

// Định nghĩa luôn kiểu Map cho gọn code
using FirmwareMap = std::map<std::string, firmware_metadata_t>;