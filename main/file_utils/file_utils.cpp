/**
 * @file file_utils.cpp
 * @brief Thao tác file tập trung qua VFS /usb (phân vùng FAT nội bộ)
 *
 * Thay thế Arduino SD library bằng POSIX API (fopen/fclose/stat/opendir...)
 * Mount point: /usb — tất cả path truyền vào dạng "/relative/path"
 * Hàm fu_vfs_path() tự prepend "/usb" trước khi gọi syscall.
 */

// ============================================================
// INCLUDES
// ============================================================
#include "file_utils.h"
#include "../usb_drive/usb_drive.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <vector>

static const char* TAG = "FILE_UTILS";

// ============================================================
// INTERNAL HELPER
// ============================================================

/**
 * Chuyển path tương đối "/file.txt" → "/usb/file.txt"
 * Đảm bảo path bắt đầu bằng "/" trước khi ghép mount point
 */
static std::string fu_vfs_path(const char* rel_path) {
    std::string p = fu_normalize_path(rel_path);  // đảm bảo leading "/"
    return std::string(USB_DRIVE_MOUNT) + p;
}

// ============================================================
// FILE OPERATIONS
// ============================================================

bool fu_file_exists(const char* path) {
    std::string full = fu_vfs_path(path);
    struct stat st;
    return (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode));
}

esp_err_t fu_file_read(const char* path, std::string& out_content) {
    std::string full = fu_vfs_path(path);
    out_content.clear();

    struct stat st;
    if (stat(full.c_str(), &st) != 0) {
        ESP_LOGW(TAG, "Not found: %s", full.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    FILE* f = fopen(full.c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "Open fail: %s", full.c_str());
        return ESP_FAIL;
    }

    out_content.resize((size_t)st.st_size);
    size_t read = fread(&out_content[0], 1, (size_t)st.st_size, f);
    fclose(f);
    out_content.resize(read);

    ESP_LOGD(TAG, "Read %d bytes: %s", (int)read, full.c_str());
    return ESP_OK;
}

esp_err_t fu_file_read_buf(const char* path, char* buffer, size_t buffer_size,
                            const char* default_value) {
    std::string full = fu_vfs_path(path);
    bool valid = false;

    FILE* f = fopen(full.c_str(), "r");
    if (f) {
        if (fgets(buffer, (int)buffer_size, f)) {
            // Trim newline
            size_t len = strlen(buffer);
            while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
                buffer[--len] = '\0';
            }
            if (len > 0) {
                valid = true;
                ESP_LOGD(TAG, "Read buf: %s = %s", full.c_str(), buffer);
            }
        }
        fclose(f);
    }

    if (!valid && default_value) {
        ESP_LOGW(TAG, "Using default: %s", full.c_str());
        strncpy(buffer, default_value, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t fu_file_write(const char* path, const char* content) {
    std::string full = fu_vfs_path(path);

    // Đảm bảo thư mục cha tồn tại
    std::string parent = fu_get_parent_dir(full.c_str());
    if (!parent.empty() && parent != USB_DRIVE_MOUNT) {
        // Tạo thư mục bỏ qua lỗi nếu đã tồn tại
        mkdir(parent.c_str(), 0755);
    }

    FILE* f = fopen(full.c_str(), "wb");
    if (!f) {
        ESP_LOGE(TAG, "Create fail: %s", full.c_str());
        return ESP_FAIL;
    }

    fputs(content, f);
    fclose(f);
    ESP_LOGD(TAG, "Written: %s", full.c_str());
    return ESP_OK;
}

esp_err_t fu_file_append(const char* path, const char* content) {
    std::string full = fu_vfs_path(path);

    FILE* f = fopen(full.c_str(), "ab");
    if (!f) {
        ESP_LOGE(TAG, "Append open fail: %s", full.c_str());
        return ESP_FAIL;
    }

    fputs(content, f);
    fclose(f);
    return ESP_OK;
}

esp_err_t fu_file_delete(const char* path) {
    std::string full = fu_vfs_path(path);

    if (remove(full.c_str()) == 0) {
        ESP_LOGI(TAG, "Deleted: %s", full.c_str());
        return ESP_OK;
    }
    // Không có file cũng OK
    struct stat st;
    if (stat(full.c_str(), &st) != 0) return ESP_OK;

    ESP_LOGE(TAG, "Delete fail: %s", full.c_str());
    return ESP_FAIL;
}

int32_t fu_file_size(const char* path) {
    std::string full = fu_vfs_path(path);
    struct stat st;
    if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (int32_t)st.st_size;
}

// ============================================================
// DIRECTORY OPERATIONS
// ============================================================

bool fu_dir_exists(const char* path) {
    std::string full = fu_vfs_path(path);
    struct stat st;
    return (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

esp_err_t fu_dir_create(const char* path) {
    std::string full = fu_vfs_path(path);

    struct stat st;
    if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return ESP_OK;

    // Tạo parent trước (đệ quy, dùng full path)
    std::string parent = fu_get_parent_dir(full.c_str());
    if (!parent.empty() && parent != USB_DRIVE_MOUNT) {
        // Kiểm tra cha chưa tồn tại thì tạo (bỏ qua lỗi)
        struct stat pst;
        if (stat(parent.c_str(), &pst) != 0) {
            mkdir(parent.c_str(), 0755);
        }
    }

    if (mkdir(full.c_str(), 0755) == 0) {
        ESP_LOGI(TAG, "Created dir: %s", full.c_str());
        return ESP_OK;
    }
    ESP_LOGE(TAG, "mkdir fail: %s", full.c_str());
    return ESP_FAIL;
}

esp_err_t fu_dir_delete(const char* path) {
    std::string full = fu_vfs_path(path);
    struct stat st;
    if (stat(full.c_str(), &st) != 0) return ESP_OK;

    if (rmdir(full.c_str()) == 0) {
        ESP_LOGI(TAG, "Removed dir: %s", full.c_str());
        return ESP_OK;
    }
    ESP_LOGW(TAG, "rmdir fail (not empty?): %s", full.c_str());
    return ESP_FAIL;
}

esp_err_t fu_dir_delete_recursive(const char* path) {
    std::string full = fu_vfs_path(path);

    DIR* d = opendir(full.c_str());
    if (!d) return ESP_OK;

    std::vector<std::string> files;
    std::vector<std::string> subdirs;

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        std::string child = full + "/" + ent->d_name;
        struct stat cst;
        if (stat(child.c_str(), &cst) == 0) {
            if (S_ISDIR(cst.st_mode)) subdirs.push_back(child);
            else                       files.push_back(child);
        }
    }
    closedir(d);

    for (auto& f : files) remove(f.c_str());
    for (auto& s : subdirs) fu_dir_delete_recursive(s.c_str());  // Đệ quy

    rmdir(full.c_str());
    return ESP_OK;
}

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

std::string fu_get_parent_dir(const char* path) {
    std::string p(path);
    while (p.length() > 1 && p.back() == '/') p.pop_back();
    size_t pos = p.rfind('/');
    if (pos == std::string::npos || pos == 0) return "";
    return p.substr(0, pos);
}

std::string fu_get_filename(const char* path) {
    std::string p(path);
    size_t pos = p.rfind('/');
    if (pos == std::string::npos) return p;
    return p.substr(pos + 1);
}

std::string fu_normalize_path(const char* path) {
    if (!path || strlen(path) == 0) return "/";
    std::string p(path);
    if (p[0] != '/') p = "/" + p;
    while (p.length() > 1 && p.back() == '/') p.pop_back();
    return p;
}
