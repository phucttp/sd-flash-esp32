/**
 * @file net_server.h
 * @brief HTTP server + mDNS cho NetFlash remote API
 *
 * Endpoints:
 *   GET  /ping          → id, ip, uptime, netflash state
 *   GET  /api/status    → busy, progress, status text
 *   GET  /api/fw_list   → array {id, display}
 *   POST /api/flash     → body {"fw_id":"..."} → 202 Accepted (async)
 *   POST /api/reboot    → 200 + restart
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khoi dong HTTP server + mDNS. Goi sau khi WiFi da ket noi.
 * @return ESP_OK neu thanh cong
 */
esp_err_t net_server_start(void);

/**
 * @brief Dung HTTP server + giai phong mDNS.
 */
void net_server_stop(void);

/**
 * @brief Kiem tra server dang chay hay khong.
 */
bool net_server_is_running(void);

/**
 * @brief Lay Node ID (e.g. "flashporter-A1B2"). Phai goi sau net_server_start().
 */
const char* net_server_get_node_id(void);

#ifdef __cplusplus
}
#endif
