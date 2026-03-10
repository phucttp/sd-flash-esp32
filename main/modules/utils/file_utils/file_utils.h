/**
 * @file file_utils.h
 * @brief Centralized file operations for SD card
 * @details All file I/O should go through this module to:
 *          - Centralize error handling
 *          - Add logging
 *          - Easier to maintain and debug
 */

#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <Arduino.h>
#include "esp_err.h"
#include <string>

// ============================================================
// FILE OPERATIONS
// ============================================================

/**
 * @brief Check if file exists
 * @param path Full path to file (e.g., "/config/url.txt")
 * @return true if exists, false otherwise
 */
bool fu_file_exists(const char* path);

/**
 * @brief Read entire file content into string
 * @param path Full path to file
 * @param out_content Output string (will be cleared first)
 * @return ESP_OK if success, ESP_ERR_NOT_FOUND if not exists, ESP_FAIL on error
 */
esp_err_t fu_file_read(const char* path, std::string& out_content);

/**
 * @brief Read file content into buffer (with max size)
 * @param path Full path to file
 * @param buffer Output buffer
 * @param buffer_size Max bytes to read
 * @param default_value Value to use if file doesn't exist (can be NULL)
 * @return ESP_OK if success
 */
esp_err_t fu_file_read_buf(const char* path, char* buffer, size_t buffer_size, const char* default_value);

/**
 * @brief Write string to file (overwrites existing)
 * @param path Full path to file
 * @param content Content to write
 * @return ESP_OK if success
 */
esp_err_t fu_file_write(const char* path, const char* content);

/**
 * @brief Append string to file
 * @param path Full path to file
 * @param content Content to append
 * @return ESP_OK if success
 */
esp_err_t fu_file_append(const char* path, const char* content);

/**
 * @brief Delete a file
 * @param path Full path to file
 * @return ESP_OK if deleted or didn't exist, ESP_FAIL on error
 */
esp_err_t fu_file_delete(const char* path);

/**
 * @brief Get file size in bytes
 * @param path Full path to file
 * @return File size, or -1 if not exists/error
 */
int32_t fu_file_size(const char* path);

// ============================================================
// DIRECTORY OPERATIONS
// ============================================================

/**
 * @brief Check if directory exists
 * @param path Full path to directory
 * @return true if exists and is directory
 */
bool fu_dir_exists(const char* path);

/**
 * @brief Create directory (and parent dirs if needed)
 * @param path Full path to directory
 * @return ESP_OK if created or already exists
 */
esp_err_t fu_dir_create(const char* path);

/**
 * @brief Delete empty directory
 * @param path Full path to directory
 * @return ESP_OK if deleted or didn't exist
 */
esp_err_t fu_dir_delete(const char* path);

/**
 * @brief Delete directory and ALL contents (recursive)
 * @param path Full path to directory
 * @return ESP_OK if deleted, ESP_FAIL on error
 * @warning This will delete everything inside!
 */
esp_err_t fu_dir_delete_recursive(const char* path);

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

/**
 * @brief Get parent directory from path
 * @param path Full path (e.g., "/FW_V1/app.bin")
 * @return Parent path (e.g., "/FW_V1"), empty if no parent
 */
std::string fu_get_parent_dir(const char* path);

/**
 * @brief Get filename from path
 * @param path Full path (e.g., "/FW_V1/app.bin")
 * @return Filename (e.g., "app.bin")
 */
std::string fu_get_filename(const char* path);

/**
 * @brief Ensure path starts with /
 * @param path Input path
 * @return Normalized path with leading /
 */
std::string fu_normalize_path(const char* path);

#endif // FILE_UTILS_H