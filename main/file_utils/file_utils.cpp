/**
 * @file file_utils.cpp
 * @brief Implementation of centralized file operations
 */

#include "file_utils.h"
#include "FS.h"
#include "SD.h"
#include "esp_log.h"
#include <vector>

static const char* TAG = "FILE_UTILS";

// ============================================================
// FILE OPERATIONS
// ============================================================

bool fu_file_exists(const char* path) {
    std::string normalized = fu_normalize_path(path);
    return SD.exists(normalized.c_str());
}

esp_err_t fu_file_read(const char* path, std::string& out_content) {
    std::string normalized = fu_normalize_path(path);
    out_content.clear();

    if (!SD.exists(normalized.c_str())) {
        ESP_LOGW(TAG, "File not found: %s", normalized.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    File file = SD.open(normalized.c_str(), FILE_READ);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", normalized.c_str());
        return ESP_FAIL;
    }

    out_content.reserve(file.size());
    while (file.available()) {
        out_content += (char)file.read();
    }
    file.close();

    ESP_LOGD(TAG, "Read %d bytes from %s", out_content.length(), normalized.c_str());
    return ESP_OK;
}

esp_err_t fu_file_read_buf(const char* path, char* buffer, size_t buffer_size, const char* default_value) {
    std::string normalized = fu_normalize_path(path);
    bool has_valid_data = false;

    if (SD.exists(normalized.c_str())) {
        File file = SD.open(normalized.c_str(), FILE_READ);
        if (file) {
            if (file.size() > 0) {
                String s = file.readStringUntil('\n');
                s.trim();
                if (s.length() > 0) {
                    strncpy(buffer, s.c_str(), buffer_size - 1);
                    buffer[buffer_size - 1] = '\0';
                    has_valid_data = true;
                    ESP_LOGD(TAG, "Read from %s: %s", normalized.c_str(), buffer);
                }
            }
            file.close();
        }
    }

    if (!has_valid_data && default_value != NULL) {
        ESP_LOGW(TAG, "Using default for %s", normalized.c_str());
        strncpy(buffer, default_value, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    }

    return ESP_OK;
}

esp_err_t fu_file_write(const char* path, const char* content) {
    std::string normalized = fu_normalize_path(path);

    // Ensure parent directory exists
    std::string parent = fu_get_parent_dir(normalized.c_str());
    if (!parent.empty()) {
        fu_dir_create(parent.c_str());
    }

    // Remove existing file
    if (SD.exists(normalized.c_str())) {
        SD.remove(normalized.c_str());
    }

    File file = SD.open(normalized.c_str(), FILE_WRITE);
    if (!file) {
        ESP_LOGE(TAG, "Failed to create file: %s", normalized.c_str());
        return ESP_FAIL;
    }

    file.print(content);
    file.close();

    ESP_LOGD(TAG, "Written to %s", normalized.c_str());
    return ESP_OK;
}

esp_err_t fu_file_append(const char* path, const char* content) {
    std::string normalized = fu_normalize_path(path);

    File file = SD.open(normalized.c_str(), FILE_APPEND);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open for append: %s", normalized.c_str());
        return ESP_FAIL;
    }

    file.print(content);
    file.close();
    return ESP_OK;
}

esp_err_t fu_file_delete(const char* path) {
    std::string normalized = fu_normalize_path(path);

    if (!SD.exists(normalized.c_str())) {
        ESP_LOGD(TAG, "File already gone: %s", normalized.c_str());
        return ESP_OK;
    }

    if (SD.remove(normalized.c_str())) {
        ESP_LOGI(TAG, "Deleted: %s", normalized.c_str());
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to delete: %s", normalized.c_str());
        return ESP_FAIL;
    }
}

int32_t fu_file_size(const char* path) {
    std::string normalized = fu_normalize_path(path);

    if (!SD.exists(normalized.c_str())) {
        return -1;
    }

    File file = SD.open(normalized.c_str(), FILE_READ);
    if (!file) {
        return -1;
    }

    int32_t size = file.size();
    file.close();
    return size;
}

// ============================================================
// DIRECTORY OPERATIONS
// ============================================================

bool fu_dir_exists(const char* path) {
    std::string normalized = fu_normalize_path(path);

    File dir = SD.open(normalized.c_str());
    if (!dir) {
        return false;
    }
    bool isDir = dir.isDirectory();
    dir.close();
    return isDir;
}

esp_err_t fu_dir_create(const char* path) {
    std::string normalized = fu_normalize_path(path);

    if (fu_dir_exists(normalized.c_str())) {
        return ESP_OK;
    }

    // Create parent directories first (recursive)
    std::string parent = fu_get_parent_dir(normalized.c_str());
    if (!parent.empty() && parent != "/") {
        fu_dir_create(parent.c_str());
    }

    if (SD.mkdir(normalized.c_str())) {
        ESP_LOGI(TAG, "Created dir: %s", normalized.c_str());
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to create dir: %s", normalized.c_str());
        return ESP_FAIL;
    }
}

esp_err_t fu_dir_delete(const char* path) {
    std::string normalized = fu_normalize_path(path);

    if (!fu_dir_exists(normalized.c_str())) {
        return ESP_OK;
    }

    if (SD.rmdir(normalized.c_str())) {
        ESP_LOGI(TAG, "Removed dir: %s", normalized.c_str());
        return ESP_OK;
    } else {
        // Might not be empty
        ESP_LOGW(TAG, "Could not remove dir (not empty?): %s", normalized.c_str());
        return ESP_FAIL;
    }
}

esp_err_t fu_dir_delete_recursive(const char* path) {
    std::string normalized = fu_normalize_path(path);

    File dir = SD.open(normalized.c_str());
    if (!dir || !dir.isDirectory()) {
        dir.close();
        return ESP_OK; // Nothing to delete
    }

    // Collect all files and subdirs
    std::vector<std::string> files;
    std::vector<std::string> subdirs;

    File entry;
    while ((entry = dir.openNextFile())) {
        String entryPath = String(normalized.c_str()) + "/" + entry.name();
        if (entry.isDirectory()) {
            subdirs.push_back(entryPath.c_str());
        } else {
            files.push_back(entryPath.c_str());
        }
        entry.close();
    }
    dir.close();

    // Delete all files first
    for (const auto& f : files) {
        fu_file_delete(f.c_str());
    }

    // Recursively delete subdirs
    for (const auto& d : subdirs) {
        fu_dir_delete_recursive(d.c_str());
    }

    // Now delete the directory itself
    return fu_dir_delete(normalized.c_str());
}

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

std::string fu_get_parent_dir(const char* path) {
    std::string p(path);

    // Remove trailing slash
    while (p.length() > 1 && p.back() == '/') {
        p.pop_back();
    }

    size_t pos = p.rfind('/');
    if (pos == std::string::npos || pos == 0) {
        return "";
    }
    return p.substr(0, pos);
}

std::string fu_get_filename(const char* path) {
    std::string p(path);
    size_t pos = p.rfind('/');
    if (pos == std::string::npos) {
        return p;
    }
    return p.substr(pos + 1);
}

std::string fu_normalize_path(const char* path) {
    if (path == NULL || strlen(path) == 0) {
        return "/";
    }

    std::string p(path);

    // Ensure leading slash
    if (p[0] != '/') {
        p = "/" + p;
    }

    // Remove trailing slash (except for root)
    while (p.length() > 1 && p.back() == '/') {
        p.pop_back();
    }

    return p;
}