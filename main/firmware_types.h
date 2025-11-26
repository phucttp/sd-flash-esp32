// firmware_types.h
#pragma once
#include <string>
#include <map>

typedef struct {
    std::string device_type;
    std::string version;
    std::string path;
    std::string md5;
    std::string path_bootloader;
    std::string md5_bootloader;
    std::string path_partition;
    std::string md5_partition;
} firmware_metadata_t;

// Định nghĩa luôn kiểu Map cho gọn code
using FirmwareMap = std::map<std::string, firmware_metadata_t>;