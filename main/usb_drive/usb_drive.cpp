/**
 * @file usb_drive.cpp
 * @brief USB MSC + CDC — mount FAT nội bộ, expose cho PC qua USB
 *
 * Dùng esp_tinyusb v2.1.0 native API (không qua Arduino USBMSC wrapper).
 *
 * Kiến trúc:
 *   - wl_mount() → wear levelling trên partition "fatfs"
 *   - tinyusb_msc_new_storage_spiflash() → quản lý FAT + USB MSC read/write
 *   - tinyusb_msc_set_storage_mount_point() → chuyển quyền truy cập APP <-> USB
 *   - tinyusb_driver_install() → khởi động TinyUSB device stack (MSC + CDC)
 *   - tinyusb_cdcacm_init() → cổng COM ảo để debug
 *
 * Mount mode:
 *   - MOUNT_APP: ESP32 đọc file firmware qua VFS (/usb/xxx.bin)
 *   - MOUNT_USB: PC thấy ổ đĩa USB, copy/xóa file
 */

// ============================================================
// INCLUDES
// ============================================================
#include "usb_drive.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "wear_levelling.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include <string.h>

static const char* TAG = "USB_DRIVE";

// ============================================================
// STATE
// ============================================================
static wl_handle_t                   s_wl_handle   = WL_INVALID_HANDLE;
static tinyusb_msc_storage_handle_t  s_storage_hdl = NULL;
static bool                          s_initialized = false;
static int                           s_lock_count  = 0;  // ref-count for nested lock

// ============================================================
// MSC EVENT CALLBACK
// ============================================================

static void msc_event_cb(tinyusb_msc_storage_handle_t handle,
                          tinyusb_msc_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_MSC_EVENT_MOUNT_START:
        ESP_LOGI(TAG, "Storage mount start -> %s",
                 event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB ? "USB" : "APP");
        break;
    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
        ESP_LOGI(TAG, "Storage mount complete -> %s",
                 event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB ? "USB" : "APP");
        break;
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
        ESP_LOGE(TAG, "Storage mount FAILED");
        break;
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        ESP_LOGW(TAG, "Storage format required (first boot?)");
        break;
    case TINYUSB_MSC_EVENT_FORMAT_FAILED:
        ESP_LOGE(TAG, "Storage format FAILED");
        break;
    default:
        break;
    }
}

// ============================================================
// PUBLIC API
// ============================================================

esp_err_t usb_drive_init(void) {
    esp_err_t err;

    // --- 1. Install MSC driver (auto_mount_off: ESP32 controls mount switching) ---
    tinyusb_msc_driver_config_t driver_cfg = {};
    driver_cfg.callback = msc_event_cb;
    driver_cfg.user_flags.auto_mount_off = 1;
    err = tinyusb_msc_install_driver(&driver_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MSC driver install failed: 0x%x", err);
        return err;
    }

    // --- 2. Mount Wear Levelling on "fatfs" partition ---
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "fatfs");
    if (!part) {
        ESP_LOGE(TAG, "Partition 'fatfs' not found");
        return ESP_ERR_NOT_FOUND;
    }

    err = wl_mount(part, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WL mount failed: 0x%x", err);
        return err;
    }

    size_t wl_total = wl_size(s_wl_handle);
    ESP_LOGI(TAG, "WL mounted: %.1f MB (%u sectors)",
             wl_total / 1048576.0f,
             (unsigned)(wl_total / wl_sector_size(s_wl_handle)));

    // --- 3. Create SPI Flash storage (FAT auto-format, mount APP) ---
    tinyusb_msc_storage_config_t storage_cfg = {};
    storage_cfg.medium.wl_handle = s_wl_handle;
    storage_cfg.fat_fs.base_path = (char*)USB_DRIVE_MOUNT;
    storage_cfg.fat_fs.config.format_if_mount_failed   = true;
    storage_cfg.fat_fs.config.max_files                = 10;
    storage_cfg.fat_fs.config.allocation_unit_size     = 4096;
    storage_cfg.fat_fs.config.disk_status_check_enable = false;
    storage_cfg.fat_fs.do_not_format = false;
    storage_cfg.mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP;

    err = tinyusb_msc_new_storage_spiflash(&storage_cfg, &s_storage_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MSC storage create failed: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "FAT mounted at %s (APP mode)", USB_DRIVE_MOUNT);

    // --- 4. Install TinyUSB driver (MSC + CDC from sdkconfig) ---
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed: 0x%x", err);
        return err;
    }

    // --- 5. Init CDC ACM for console/debug ---
    tinyusb_config_cdcacm_t cdc_cfg = {};
    cdc_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    err = tinyusb_cdcacm_init(&cdc_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CDC init failed: 0x%x (non-fatal)", err);
    } else {
        // Redirect stdout/stderr → USB CDC ACM
        // (USB Serial JTAG bị kill khi TinyUSB OTG start — cùng GPIO19/20)
        tinyusb_console_init(TINYUSB_CDC_ACM_0);
    }

    s_initialized = true;
    s_lock_count = 1;  // Init mounts in APP mode = locked
    ESP_LOGI(TAG, "USB drive ready (MSC + CDC)");
    return ESP_OK;
}

void usb_drive_lock(void) {
    if (s_storage_hdl) {
        s_lock_count++;
        if (s_lock_count == 1) {
            // Actually switch to APP mode (first lock)
            esp_err_t err = tinyusb_msc_set_storage_mount_point(
                s_storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_APP);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "MSC locked -> APP mode (ESP reads, PC blocked)");
            } else {
                ESP_LOGE(TAG, "MSC lock failed: 0x%x", err);
            }
        } else {
            ESP_LOGD(TAG, "MSC lock nested (count=%d)", s_lock_count);
        }
    }
}

void usb_drive_unlock(void) {
    if (s_storage_hdl) {
        if (s_lock_count > 0) s_lock_count--;
        if (s_lock_count == 0) {
            // Actually switch to USB mode (last unlock)
            esp_err_t err = tinyusb_msc_set_storage_mount_point(
                s_storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_USB);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "MSC unlocked -> USB mode (PC can access drive)");
            } else {
                ESP_LOGE(TAG, "MSC unlock failed: 0x%x", err);
            }
        } else {
            ESP_LOGD(TAG, "MSC unlock nested (count=%d)", s_lock_count);
        }
    }
}

bool usb_drive_is_ready(void) {
    return s_initialized && (s_wl_handle != WL_INVALID_HANDLE);
}

void usb_drive_get_status(bool *connected, bool *mounted) {
    if (connected) *connected = tud_connected();
    if (mounted)   *mounted   = tud_mounted();
}
