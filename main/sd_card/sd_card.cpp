/**
 * @file sd_card.cpp
 * @brief Quản lý firmware store — quét thư mục trên USB drive nội bộ
 *
 * Chức năng chính:
 *   - Quét thư mục gốc /usb: prefix ST_ = STM32, prefix ES_ = ESP32
 *   - Build metadata và menu từ tên thư mục (không cần index.json)
 *   - Lưu/load lịch sử nạp firmware (/usb/history.txt)
 *
 * Quy ước thư mục trên USB drive:
 *   ST_<tên>/FW.bin                 → STM32 target
 *   ES_<tên>/app.bin + boot.bin + part.bin → ESP32 target
 */

// ============================================================
// INCLUDES
// ============================================================
#include "sd_card.h"
#include "../usb_drive/usb_drive.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <vector>
#include <map>

// ============================================================
// CONSTANTS & GLOBALS
// ============================================================
static const char* TAG             = "FW_STORE";
static const char* HISTORY_PATH    = USB_DRIVE_MOUNT "/history.txt";
static const int   HISTORY_MAX     = 10;

bool g_is_sd_mounted = false;
std::map<std::string, firmware_metadata_t> g_firmware_map;

// Menu arrays
static std::vector<std::string>   g_displayStrings;
static std::vector<std::string>   g_idStrings;
static std::vector<const char*>   g_menuDisplayPtrs;
static std::vector<const char*>   g_menuIdPtrs;

// History arrays
static std::vector<std::string>   g_histIdStrings;
static std::vector<std::string>   g_histDisplayStrings;
static std::vector<const char*>   g_histIdPtrs;
static std::vector<const char*>   g_histDisplayPtrs;

// ============================================================
// INTERNAL HELPER
// ============================================================

/**
 * Kiểm tra file tồn tại trong VFS (full path)
 */
static bool file_exists_full(const char* full_path) {
    struct stat st;
    return (stat(full_path, &st) == 0 && S_ISREG(st.st_mode));
}

/**
 * Auto-detect ESP32 firmware files bằng magic bytes + size.
 *   Partition table: magic 0xAA50 ở offset 0
 *   Bootloader:      magic 0xE9 + size < 64KB
 *   Application:     magic 0xE9 + size >= 64KB
 *
 * @param dir_full  Full path tới folder (e.g., "/usb/ES_MyFW")
 * @param rel_dir   Relative folder name (e.g., "ES_MyFW")
 * @param meta      Output metadata — path, path_bootloader, path_partition
 * @return true nếu tìm được ít nhất app file
 */
#define ESP_IMG_MAGIC   0xE9
#define PART_MAGIC_0    0xAA
#define PART_MAGIC_1    0x50
#define BOOT_SIZE_LIMIT 65536  // 64KB — bootloader luôn < 40KB, app luôn > 100KB

static bool detect_esp32_files(const char* dir_full, const char* rel_dir,
                                firmware_metadata_t& meta) {
    DIR* d = opendir(dir_full);
    if (!d) return false;

    meta.path.clear();
    meta.path_bootloader.clear();
    meta.path_partition.clear();

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_type == DT_DIR) continue;

        // Chỉ xét file .bin
        const char* name = ent->d_name;
        size_t nlen = strlen(name);
        if (nlen < 5 || strcasecmp(name + nlen - 4, ".bin") != 0) continue;

        // Lấy size
        char fpath[300];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir_full, name);
        struct stat st;
        if (stat(fpath, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        size_t fsize = (size_t)st.st_size;
        if (fsize < 4) continue;  // Quá nhỏ

        // Đọc 2 bytes đầu
        FILE* f = fopen(fpath, "rb");
        if (!f) continue;
        uint8_t hdr[2] = {0};
        fread(hdr, 1, 2, f);
        fclose(f);

        // Relative path cho metadata
        char rel_path[300];
        snprintf(rel_path, sizeof(rel_path), "/%s/%s", rel_dir, name);

        if (hdr[0] == PART_MAGIC_0 && hdr[1] == PART_MAGIC_1) {
            // Partition table
            meta.path_partition = rel_path;
            ESP_LOGI(TAG, "  [PART] %s (%zu bytes)", name, fsize);
        } else if (hdr[0] == ESP_IMG_MAGIC) {
            if (fsize < BOOT_SIZE_LIMIT) {
                // Bootloader (ESP image nhỏ)
                meta.path_bootloader = rel_path;
                ESP_LOGI(TAG, "  [BOOT] %s (%zu bytes)", name, fsize);
            } else {
                // Application (ESP image lớn)
                meta.path = rel_path;
                ESP_LOGI(TAG, "  [APP]  %s (%zu bytes)", name, fsize);
            }
        } else {
            ESP_LOGD(TAG, "  [SKIP] %s (magic=0x%02X%02X)", name, hdr[0], hdr[1]);
        }
    }
    closedir(d);

    if (meta.path.empty()) {
        ESP_LOGW(TAG, "No app binary found in %s", rel_dir);
        return false;
    }
    if (meta.path_bootloader.empty()) {
        ESP_LOGW(TAG, "No bootloader found in %s — app-only flash", rel_dir);
    }
    if (meta.path_partition.empty()) {
        ESP_LOGW(TAG, "No partition table found in %s — app-only flash", rel_dir);
    }

    return true;
}

// ============================================================
// PUBLIC API: MOUNT
// ============================================================

/**
 * sd_mount: không cần cs_pin nữa — USB drive đã được init bởi usb_drive_init().
 * Hàm này chỉ xác nhận drive đã sẵn sàng.
 */
esp_err_t sd_mount(int cs_pin) {
    (void)cs_pin;  // Không dùng — USB drive không có SPI CS
    if (!usb_drive_is_ready()) {
        ESP_LOGE(TAG, "USB drive not ready");
        g_is_sd_mounted = false;
        return ESP_FAIL;
    }
    g_is_sd_mounted = true;
    ESP_LOGI(TAG, "Firmware store ready at %s", USB_DRIVE_MOUNT);
    return ESP_OK;
}

esp_err_t sd_unmount() {
    g_is_sd_mounted = false;
    return ESP_OK;
}

// ============================================================
// PUBLIC API: LOAD METADATA (folder scan)
// ============================================================

/**
 * Quét thư mục gốc USB drive — mỗi subfolder là 1 firmware entry
 *
 * Prefix quy ước:
 *   ST_ → STM32 (FW.bin bên trong)
 *   ES_ → ESP32 (app.bin + boot.bin + part.bin)
 */
esp_err_t sd_load_metadata() {
    if (!g_is_sd_mounted) {
        ESP_LOGE(TAG, "Drive not mounted");
        return ESP_FAIL;
    }

    // Clear dữ liệu cũ
    g_firmware_map.clear();
    g_displayStrings.clear();
    g_idStrings.clear();
    g_menuDisplayPtrs.clear();
    g_menuIdPtrs.clear();

    DIR* root = opendir(USB_DRIVE_MOUNT);
    if (!root) {
        ESP_LOGE(TAG, "Cannot open %s", USB_DRIVE_MOUNT);
        goto build_menu;
    }

    {
        int idx = 1;
        struct dirent* ent;
        while ((ent = readdir(root)) != nullptr) {
            if (ent->d_type != DT_DIR) continue;

            const char* name = ent->d_name;
            if (name[0] == '.') continue;  // Bỏ qua . và ..

            bool is_stm32 = (strncmp(name, "ST_", 3) == 0);
            bool is_esp32 = (strncmp(name, "ES_", 3) == 0);

            if (!is_stm32 && !is_esp32) {
                ESP_LOGD(TAG, "Skipping unknown prefix: %s", name);
                continue;
            }

            // Build đường dẫn base
            std::string base = std::string(USB_DRIVE_MOUNT) + "/" + name;
            firmware_metadata_t meta = {};
            meta.encrypted = false;

            if (is_stm32) {
                meta.device_type = "STM32";
                // Scan folder tìm file .bin đầu tiên (đặt tên gì cũng được)
                DIR* stdir = opendir(base.c_str());
                bool found_bin = false;
                if (stdir) {
                    struct dirent* stent;
                    while ((stent = readdir(stdir)) != NULL) {
                        if (stent->d_type != DT_REG) continue;
                        const char* dot = strrchr(stent->d_name, '.');
                        if (dot && strcasecmp(dot, ".bin") == 0) {
                            meta.path = std::string("/") + name + "/" + stent->d_name;
                            ESP_LOGI(TAG, "ST_ found: %s", meta.path.c_str());
                            found_bin = true;
                            break;
                        }
                    }
                    closedir(stdir);
                }
                if (!found_bin) {
                    ESP_LOGW(TAG, "ST_ folder has no .bin file: %s", name);
                    continue;
                }
            } else {
                meta.device_type = "ESP32";
                ESP_LOGI(TAG, "Scanning ESP32 folder: %s", name);
                if (!detect_esp32_files(base.c_str(), name, meta)) {
                    ESP_LOGW(TAG, "ES_ folder has no valid app binary: %s", name);
                    continue;
                }
            }

            // Tên folder = fw_id = display name
            std::string fw_id      = name;
            std::string displayName = std::to_string(idx++) + ". " + name;

            g_firmware_map[fw_id]  = meta;
            g_displayStrings.push_back(displayName);
            g_idStrings.push_back(fw_id);

            ESP_LOGI(TAG, "Found: %s [%s]", name, meta.device_type.c_str());
        }
        closedir(root);
    }

build_menu:
    for (const auto& s : g_displayStrings) g_menuDisplayPtrs.push_back(s.c_str());
    for (const auto& s : g_idStrings)      g_menuIdPtrs.push_back(s.c_str());

    if (g_displayStrings.empty()) {
        g_displayStrings.push_back("(No Firmware)");
        g_idStrings.push_back("NO_FW");
        g_menuDisplayPtrs.push_back(g_displayStrings.back().c_str());
        g_menuIdPtrs.push_back(g_idStrings.back().c_str());
    }

    ESP_LOGI(TAG, "Metadata loaded: %d firmware entries", (int)g_firmware_map.size());
    return ESP_OK;
}

// ============================================================
// PUBLIC API: FIRMWARE INFO
// ============================================================

esp_err_t sd_get_firmware_path(const std::string& fw_id, firmware_metadata_t& out_meta) {
    auto it = g_firmware_map.find(fw_id);
    if (it == g_firmware_map.end()) {
        ESP_LOGE(TAG, "FW not found: %s", fw_id.c_str());
        return ESP_ERR_NOT_FOUND;
    }
    out_meta = it->second;
    return ESP_OK;
}

const char** sd_get_menu_display_items(int& out_count) {
    out_count = (int)g_menuDisplayPtrs.size();
    return g_menuDisplayPtrs.data();
}

const char** sd_get_menu_id_items() {
    return g_menuIdPtrs.data();
}

/**
 * Description: dùng device_type + fw_id (folder name) làm mô tả ngắn
 */
String sd_get_description(const char* fw_id) {
    if (!fw_id) return "";
    auto it = g_firmware_map.find(fw_id);
    if (it == g_firmware_map.end()) return String(fw_id);
    return String(it->second.device_type.c_str()) + " — " + String(fw_id);
}

// ============================================================
// PUBLIC API: FLASH HISTORY (POSIX)
// ============================================================

void sd_history_add(const char* fw_id) {
    if (!g_is_sd_mounted || !fw_id || strlen(fw_id) == 0) return;

    // Đọc entries hiện có
    std::vector<std::string> entries;
    FILE* f = fopen(HISTORY_PATH, "r");
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            // Trim newline
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
            if (len > 0) entries.push_back(line);
        }
        fclose(f);
    }

    entries.push_back(fw_id);

    // Giữ tối đa HISTORY_MAX entries (xóa cũ nhất)
    if ((int)entries.size() > HISTORY_MAX) {
        entries.erase(entries.begin(),
                      entries.begin() + (int)(entries.size() - HISTORY_MAX));
    }

    f = fopen(HISTORY_PATH, "w");
    if (f) {
        for (auto& e : entries) {
            fputs(e.c_str(), f);
            fputc('\n', f);
        }
        fclose(f);
        ESP_LOGI(TAG, "History: saved '%s' (%d entries)", fw_id, (int)entries.size());
    } else {
        ESP_LOGE(TAG, "History: write failed");
    }
}

void sd_history_load() {
    g_histIdStrings.clear();
    g_histDisplayStrings.clear();
    g_histIdPtrs.clear();
    g_histDisplayPtrs.clear();

    if (!g_is_sd_mounted) return;

    std::vector<std::string> entries;
    FILE* f = fopen(HISTORY_PATH, "r");
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
            if (len > 0) entries.push_back(line);
        }
        fclose(f);
    }

    if (entries.empty()) return;

    int count = (int)entries.size();
    for (int i = 0; i < count; i++) {
        const std::string& fw_id = entries[count - 1 - i];  // Mới nhất trước
        g_histIdStrings.push_back(fw_id);

        char disp[32];
        auto it = g_firmware_map.find(fw_id);
        if (it != g_firmware_map.end()) {
            snprintf(disp, sizeof(disp), "#%d %s", i + 1, fw_id.c_str());
        } else {
            snprintf(disp, sizeof(disp), "#%d %s", i + 1, fw_id.c_str());
        }
        g_histDisplayStrings.push_back(disp);
    }

    for (auto& s : g_histIdStrings)      g_histIdPtrs.push_back(s.c_str());
    for (auto& s : g_histDisplayStrings) g_histDisplayPtrs.push_back(s.c_str());

    ESP_LOGI(TAG, "History loaded: %d entries", count);
}

const char** sd_history_get_display(int& out_count) {
    out_count = (int)g_histDisplayPtrs.size();
    return out_count > 0 ? g_histDisplayPtrs.data() : nullptr;
}

const char** sd_history_get_ids() {
    return g_histIdPtrs.empty() ? nullptr : g_histIdPtrs.data();
}
