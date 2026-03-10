/**
 * @file flash_log.cpp
 * @brief Ghi log các thao tác flash/erase/scan vào USB drive.
 *
 * Format mỗi dòng:
 *   #NNN | UP:HHhMMm | FW_ID | ACTION | RESULT | DETAIL
 *
 * Auto-rotate: giữ tối đa LOG_MAX_ENTRIES (100) dòng.
 * Khi đầy, xóa entries cũ nhất để nhường chỗ.
 */

#include "flash_log.h"
#include "../../storage/usb_drive/usb_drive.h"  // usb_drive_lock/unlock (ref-counted)
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vector>
#include <string>

static const char* TAG = "FLASH_LOG";
static const char* LOG_PATH = USB_DRIVE_MOUNT "/flashlog.txt";

#define LOG_MAX_ENTRIES 100
#define LOG_LINE_MAX    120

// ============================================================
// INTERNAL: Uptime string "HHhMMm"
// ============================================================
static void get_uptime_str(char* buf, int buf_len) {
    int64_t us = esp_timer_get_time();
    int total_sec = (int)(us / 1000000);
    int hours = total_sec / 3600;
    int mins  = (total_sec % 3600) / 60;
    snprintf(buf, buf_len, "%02dh%02dm", hours, mins);
}

// ============================================================
// INTERNAL: Đọc tất cả lines từ file
// ============================================================
static std::vector<std::string> read_all_lines() {
    std::vector<std::string> lines;
    FILE* f = fopen(LOG_PATH, "r");
    if (!f) return lines;

    char line[LOG_LINE_MAX];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len > 0) lines.push_back(line);
    }
    fclose(f);
    return lines;
}

// ============================================================
// INTERNAL: Ghi tất cả lines ra file
// ============================================================
static void write_all_lines(const std::vector<std::string>& lines) {
    ESP_LOGI(TAG, "Writing %d lines to %s", (int)lines.size(), LOG_PATH);
    FILE* f = fopen(LOG_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open log file for write! errno=%d", errno);
        return;
    }
    for (const auto& line : lines) {
        fputs(line.c_str(), f);
        fputc('\n', f);
    }
    fflush(f);
    fclose(f);
    ESP_LOGI(TAG, "Log file written OK (%d lines)", (int)lines.size());
}

// ============================================================
// INTERNAL: Lấy số entry tiếp theo từ dòng cuối
// ============================================================
static int get_next_entry_num(const std::vector<std::string>& lines) {
    if (lines.empty()) return 1;

    // Parse "#NNN" từ dòng cuối
    const char* last = lines.back().c_str();
    if (last[0] == '#') {
        int num = atoi(last + 1);
        if (num > 0) return num + 1;
    }
    return (int)lines.size() + 1;
}

// ============================================================
// PUBLIC API
// ============================================================

void flash_log_write(const char* action, const char* fw_id,
                     const char* result, const char* detail) {
    if (!action || !result) return;

    // Lock USB → APP mode (ref-counted, safe for nested calls)
    usb_drive_lock();

    // Uptime
    char uptime[16];
    get_uptime_str(uptime, sizeof(uptime));

    // Đọc entries hiện có
    auto lines = read_all_lines();
    int entry_num = get_next_entry_num(lines);

    // Build log line
    char entry[LOG_LINE_MAX];
    if (detail && strlen(detail) > 0) {
        snprintf(entry, sizeof(entry), "#%03d | UP:%s | %s | %s | %s | %s",
                 entry_num, uptime,
                 fw_id ? fw_id : "-",
                 action, result, detail);
    } else {
        snprintf(entry, sizeof(entry), "#%03d | UP:%s | %s | %s | %s",
                 entry_num, uptime,
                 fw_id ? fw_id : "-",
                 action, result);
    }

    lines.push_back(entry);

    // Auto-rotate: giữ tối đa LOG_MAX_ENTRIES
    if ((int)lines.size() > LOG_MAX_ENTRIES) {
        lines.erase(lines.begin(),
                     lines.begin() + ((int)lines.size() - LOG_MAX_ENTRIES));
    }

    write_all_lines(lines);
    ESP_LOGI(TAG, "LOG: %s", entry);

    // Unlock USB → trả lại mode trước đó
    usb_drive_unlock();
}

int flash_log_read(char* buf, int buf_len) {
    if (!buf || buf_len <= 0) return 0;

    FILE* f = fopen(LOG_PATH, "r");
    if (!f) {
        buf[0] = '\0';
        return 0;
    }

    int total = (int)fread(buf, 1, buf_len - 1, f);
    buf[total] = '\0';
    fclose(f);
    return total;
}

int flash_log_count(void) {
    auto lines = read_all_lines();
    return (int)lines.size();
}

void flash_log_clear(void) {
    FILE* f = fopen(LOG_PATH, "w");
    if (f) {
        fclose(f);
        ESP_LOGI(TAG, "Log cleared");
    }
}
