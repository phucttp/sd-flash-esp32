/**
 * @file net_server.cpp
 * @brief HTTP server và mDNS cho tính năng NetFlash — nạp firmware từ xa qua mạng WiFi.
 *
 * Chức năng chính:
 *   - Khởi tạo HTTP server nội bộ trên ESP32-C3 để nhận lệnh từ desktop FlashPorter
 *   - Đăng ký mDNS hostname (flashporter-XXYY.local) để desktop tự khám phá thiết bị
 *   - Xử lý GET  /api/ping   — trả thông tin thiết bị (id, ip, uptime, netflash status)
 *   - Xử lý POST /api/flash  — nhận lệnh nạp, phản hồi 202 ngay, nạp trong background task
 *   - Xử lý GET  /api/status — trả tiến trình và kết quả nạp để client polling
 */

#include "net_server.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "esp_http_server.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../sd_card/sd_card.h"
#include "../app_actions/app_actions.h"
#include "../flasher/flasher_common.h"

static const char* TAG = "NET_SERVER";

// ============================================================
// STATE
// ============================================================
static httpd_handle_t s_server = NULL;
static bool s_running = false;
static char s_node_id[32] = "flashporter-0000";

// Background flash: fw_id buffer + pending flag (guards race between handler + task)
static char s_pending_fw_id[64] = {0};
static volatile bool s_net_flash_pending = false;

// ============================================================
// HELPERS
// ============================================================
static void get_ip_str(char* buf, size_t len) {
    esp_netif_ip_info_t ip_info = {};
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    } else {
        snprintf(buf, len, "0.0.0.0");
    }
}

static esp_err_t send_json(httpd_req_t* req, const char* json, int status_code) {
    if (status_code == 202) httpd_resp_set_status(req, "202 Accepted");
    else if (status_code == 409) httpd_resp_set_status(req, "409 Conflict");
    else if (status_code == 400) httpd_resp_set_status(req, "400 Bad Request");
    else if (status_code == 500) httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

// Simple JSON string extractor: finds "key":"value" → copies value to out
static bool json_get_string(const char* json, const char* key, char* out, size_t out_size) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    const char* end = strchr(p, '"');
    if (!end) return false;
    size_t slen = (size_t)(end - p);
    if (slen >= out_size) return false;
    memcpy(out, p, slen);
    out[slen] = '\0';
    return true;
}

// ============================================================
// BACKGROUND FLASH TASK
// ============================================================
static void net_flash_task(void* arg) {
    ESP_LOGI(TAG, ">>> net_flash_task start: %s", s_pending_fw_id);

    esp_err_t ret = action_remote_flash_firmware(s_pending_fw_id);

    if (ret == ESP_OK) {
        sd_history_add(s_pending_fw_id);
        vTaskDelay(pdMS_TO_TICKS(300));
        host_system_restart(0, s_pending_fw_id);
        // host_system_restart() calls esp_restart() internally — task never returns here
    } else {
        ESP_LOGE(TAG, ">>> net_flash_task FAILED: %d", ret);
    }

    s_net_flash_pending = false;
    vTaskDelete(NULL);
}

// ============================================================
// HTTP HANDLERS
// ============================================================

// GET /ping → {"id":"flashporter-A1B2","ip":"192.168.1.5","uptime":123,"netflash":true}
static esp_err_t handle_ping(httpd_req_t* req) {
    char ip[24] = "0.0.0.0";
    get_ip_str(ip, sizeof(ip));
    uint64_t uptime_sec = (uint64_t)(esp_timer_get_time() / 1000000LL);
    bool nf = action_netflash_is_enabled();

    char resp[160];
    snprintf(resp, sizeof(resp),
             "{\"id\":\"%s\",\"ip\":\"%s\",\"uptime\":%" PRIu64 ",\"netflash\":%s}",
             s_node_id, ip, uptime_sec, nf ? "true" : "false");
    return send_json(req, resp, 200);
}

// GET /api/status → {"busy":false,"progress":0,"status":"Ready"}
static esp_err_t handle_status(httpd_req_t* req) {
    const char* text = NULL;
    int progress = 0;
    bool busy = false;
    app_actions_get_status(&text, &progress, &busy);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"busy\":%s,\"progress\":%d,\"status\":\"%s\"}",
             busy ? "true" : "false", progress, text ? text : "");
    return send_json(req, resp, 200);
}

// GET /api/fw_list → {"firmwares":[{"id":"F103","display":"1.STM32 v1.0"},...]}
static esp_err_t handle_fw_list(httpd_req_t* req) {
    int count = 0;
    const char** display_items = sd_get_menu_display_items(count);
    const char** id_items = sd_get_menu_id_items();

    char* buf = (char*)malloc(4096);
    if (!buf) {
        return send_json(req, "{\"error\":\"oom\"}", 500);
    }

    int pos = 0;
    pos += snprintf(buf + pos, 4096 - pos, "{\"firmwares\":[");
    for (int i = 0; i < count && pos < 3900; i++) {
        if (i > 0) pos += snprintf(buf + pos, 4096 - pos, ",");
        pos += snprintf(buf + pos, 4096 - pos,
                        "{\"id\":\"%s\",\"display\":\"%s\"}",
                        id_items && id_items[i] ? id_items[i] : "",
                        display_items && display_items[i] ? display_items[i] : "");
    }
    pos += snprintf(buf + pos, 4096 - pos, "]}");

    esp_err_t ret = send_json(req, buf, 200);
    free(buf);
    return ret;
}

// POST /api/flash  body: {"fw_id":"F103"}
// → 202 Accepted + spawn background task, or 409 if busy
static esp_err_t handle_flash(httpd_req_t* req) {
    // Read body (max 128 bytes)
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= 128) {
        return send_json(req, "{\"error\":\"bad_request\"}", 400);
    }

    char body[128] = {0};
    int received = httpd_req_recv(req, body, content_len);
    if (received <= 0) {
        return send_json(req, "{\"error\":\"recv_fail\"}", 400);
    }
    body[received] = '\0';

    char fw_id[64] = {0};
    if (!json_get_string(body, "fw_id", fw_id, sizeof(fw_id))) {
        return send_json(req, "{\"error\":\"missing_fw_id\"}", 400);
    }

    // Check busy (both OLED action and previous net_flash)
    const char* st = NULL;
    int pr = 0;
    bool busy = false;
    app_actions_get_status(&st, &pr, &busy);
    if (busy || s_net_flash_pending) {
        return send_json(req, "{\"error\":\"busy\"}", 409);
    }

    // Copy fw_id + set pending flag before xTaskCreate to guard race
    strncpy(s_pending_fw_id, fw_id, sizeof(s_pending_fw_id) - 1);
    s_pending_fw_id[sizeof(s_pending_fw_id) - 1] = '\0';
    s_net_flash_pending = true;

    BaseType_t created = xTaskCreate(net_flash_task, "net_flash", 12288, NULL, 4, NULL);
    if (created != pdPASS) {
        s_net_flash_pending = false;
        return send_json(req, "{\"error\":\"task_create_fail\"}", 500);
    }

    ESP_LOGI(TAG, "Flash accepted: %s", fw_id);
    return send_json(req, "{\"accepted\":true}", 202);
}

// POST /api/reboot → 200 + restart
static esp_err_t handle_reboot(httpd_req_t* req) {
    send_json(req, "{\"ok\":true}", 200);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

// ============================================================
// NODE ID (NVS → generate from MAC if not found)
// ============================================================
static void load_or_create_node_id(void) {
    nvs_handle_t nvs;
    if (nvs_open("flashporter", NVS_READONLY, &nvs) == ESP_OK) {
        size_t sz = sizeof(s_node_id);
        if (nvs_get_str(nvs, "node_id", s_node_id, &sz) == ESP_OK) {
            nvs_close(nvs);
            ESP_LOGI(TAG, "Node ID from NVS: %s", s_node_id);
            return;
        }
        nvs_close(nvs);
    }

    // Generate from last 2 bytes of MAC
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_node_id, sizeof(s_node_id), "flashporter-%02X%02X", mac[4], mac[5]);
    ESP_LOGI(TAG, "Node ID generated: %s", s_node_id);

    if (nvs_open("flashporter", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "node_id", s_node_id);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

// ============================================================
// mDNS
// ============================================================
static void init_mdns(void) {
    mdns_init();
    mdns_hostname_set(s_node_id);              // → flashporter-A1B2.local
    mdns_instance_name_set("FlashPorter");
    mdns_service_add(NULL, "_flashporter", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: %s.local (port 80)", s_node_id);
}

// ============================================================
// SERVER START / STOP
// ============================================================
esp_err_t net_server_start(void) {
    if (s_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    load_or_create_node_id();
    init_mdns();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.server_port = 80;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return ESP_FAIL;
    }

    // Register URI handlers
    httpd_uri_t uri_ping     = { "/ping",        HTTP_GET,  handle_ping,    NULL };
    httpd_uri_t uri_status   = { "/api/status",  HTTP_GET,  handle_status,  NULL };
    httpd_uri_t uri_fw_list  = { "/api/fw_list", HTTP_GET,  handle_fw_list, NULL };
    httpd_uri_t uri_flash    = { "/api/flash",   HTTP_POST, handle_flash,   NULL };
    httpd_uri_t uri_reboot   = { "/api/reboot",  HTTP_POST, handle_reboot,  NULL };

    httpd_register_uri_handler(s_server, &uri_ping);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_fw_list);
    httpd_register_uri_handler(s_server, &uri_flash);
    httpd_register_uri_handler(s_server, &uri_reboot);

    s_running = true;
    ESP_LOGI(TAG, "HTTP server started, node: %s", s_node_id);
    return ESP_OK;
}

void net_server_stop(void) {
    if (!s_running) return;
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    mdns_free();
    s_running = false;
    ESP_LOGI(TAG, "HTTP server stopped");
}

bool net_server_is_running(void) {
    return s_running;
}

const char* net_server_get_node_id(void) {
    return s_node_id;
}
