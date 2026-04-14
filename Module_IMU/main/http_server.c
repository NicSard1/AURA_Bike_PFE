/*
 * http_server.c
 * Projet vélo / AURA Bike
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"
#include "sys/param.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "tasks_common.h"
#include "wifi_app.h"
#include "http_server.h"
#include "app_nvs.h"
#include "sntp_time_sync.h"
#include "bike_data.h"

static const char TAG[] = "http_server";

static int g_fw_update_status = OTA_UPDATE_PENDING;
static httpd_handle_t http_server_handle = NULL;
static TaskHandle_t task_http_server_monitor = NULL;
static QueueHandle_t http_server_monitor_queue_handle = NULL;

/* ============================================================
 * Forward declarations
 * ============================================================ */
static void http_server_monitor(void *parameter);
static void http_server_fw_update_reset_timer(void);

static esp_err_t http_server_index_html_handler(httpd_req_t *req);
static esp_err_t http_server_app_css_handler(httpd_req_t *req);
static esp_err_t http_server_app_js_handler(httpd_req_t *req);
static esp_err_t http_server_api_js_handler(httpd_req_t *req);
static esp_err_t http_server_ble_js_handler(httpd_req_t *req);
static esp_err_t http_server_kml_gpx_js_handler(httpd_req_t *req);
static esp_err_t http_server_maps_js_handler(httpd_req_t *req);
static esp_err_t http_server_route_js_handler(httpd_req_t *req);
static esp_err_t http_server_storage_js_handler(httpd_req_t *req);
static esp_err_t http_server_jquery_handler(httpd_req_t *req);
static esp_err_t http_server_favicon_ico_handler(httpd_req_t *req);

static esp_err_t http_server_sysinfo_handler(httpd_req_t *req);
static esp_err_t http_server_wifi_cfg_get_handler(httpd_req_t *req);
static esp_err_t http_server_wifi_cfg_post_handler(httpd_req_t *req);
static esp_err_t http_server_wifi_reset_post_handler(httpd_req_t *req);

static httpd_handle_t http_server_configure(void);

/* ============================================================
 * Embedded files
 * ============================================================ */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

extern const uint8_t app_css_start[] asm("_binary_app_css_start");
extern const uint8_t app_css_end[]   asm("_binary_app_css_end");

extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[]   asm("_binary_app_js_end");

extern const uint8_t api_js_start[] asm("_binary_api_js_start");
extern const uint8_t api_js_end[]   asm("_binary_api_js_end");

extern const uint8_t ble_js_start[] asm("_binary_ble_js_start");
extern const uint8_t ble_js_end[]   asm("_binary_ble_js_end");

extern const uint8_t kml_gpx_js_start[] asm("_binary_kml_gpx_js_start");
extern const uint8_t kml_gpx_js_end[]   asm("_binary_kml_gpx_js_end");

extern const uint8_t maps_js_start[] asm("_binary_maps_js_start");
extern const uint8_t maps_js_end[]   asm("_binary_maps_js_end");

extern const uint8_t route_js_start[] asm("_binary_route_js_start");
extern const uint8_t route_js_end[]   asm("_binary_route_js_end");

extern const uint8_t storage_js_start[] asm("_binary_storage_js_start");
extern const uint8_t storage_js_end[]   asm("_binary_storage_js_end");

extern const uint8_t jquery_3_3_1_min_js_start[] asm("_binary_jquery_3_3_1_min_js_start");
extern const uint8_t jquery_3_3_1_min_js_end[]   asm("_binary_jquery_3_3_1_min_js_end");

extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[]   asm("_binary_favicon_ico_end");

/* ============================================================
 * OTA reboot timer
 * ============================================================ */
static const esp_timer_create_args_t fw_update_reset_args = {
    .callback = &http_server_fw_update_reset_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "fw_update_reset"
};

static esp_timer_handle_t fw_update_reset = NULL;

/* ============================================================
 * Helpers
 * ============================================================ */
static bool json_get_string_value(const char *json, const char *key, char *out, size_t out_size)
{
    if (!json || !key || !out || out_size == 0)
    {
        return false;
    }

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *start = strstr(json, pattern);
    if (!start)
    {
        return false;
    }

    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (!end)
    {
        return false;
    }

    size_t len = (size_t)(end - start);
    if (len >= out_size)
    {
        len = out_size - 1;
    }

    memcpy(out, start, len);
    out[len] = '\0';

    return true;
}

static esp_err_t http_server_send_json_error(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

static void http_server_fw_update_reset_timer(void)
{
    if (g_fw_update_status == OTA_UPDATE_SUCCESSFUL)
    {
        ESP_LOGI(TAG, "FW updated successfully, starting reset timer");

        if (fw_update_reset == NULL)
        {
            ESP_ERROR_CHECK(esp_timer_create(&fw_update_reset_args, &fw_update_reset));
        }

        ESP_ERROR_CHECK(esp_timer_start_once(fw_update_reset, 8000000));
    }
    else
    {
        ESP_LOGI(TAG, "FW update unsuccessful, no reset timer");
    }
}

/* ============================================================
 * Monitor task
 * ============================================================ */
static void http_server_monitor(void *parameter)
{
    (void)parameter;
    http_server_queue_message_t msg;

    for (;;)
    {
        if (xQueueReceive(http_server_monitor_queue_handle, &msg, portMAX_DELAY))
        {
            switch (msg.msgID)
            {
                case HTTP_MSG_WIFI_CONNECT_INIT:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_INIT");
                    break;

                case HTTP_MSG_WIFI_CONNECT_SUCCESS:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_SUCCESS");
                    break;

                case HTTP_MSG_WIFI_CONNECT_FAIL:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_FAIL");
                    break;

                case HTTP_MSG_OTA_UPDATE_SUCCESSFUL:
                    ESP_LOGI(TAG, "HTTP_MSG_OTA_UPDATE_SUCCESSFUL");
                    g_fw_update_status = OTA_UPDATE_SUCCESSFUL;
                    http_server_fw_update_reset_timer();
                    break;

                case HTTP_MSG_OTA_UPDATE_FAILED:
                    ESP_LOGI(TAG, "HTTP_MSG_OTA_UPDATE_FAILED");
                    g_fw_update_status = OTA_UPDATE_FAILED;
                    break;

                default:
                    break;
            }
        }
    }
}

/* ============================================================
 * Static files handlers
 * ============================================================ */
static esp_err_t http_server_index_html_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
                    (const char *)index_html_start,
                    (ssize_t)(index_html_end - index_html_start));
    return ESP_OK;
}

static esp_err_t http_server_app_css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req,
                    (const char *)app_css_start,
                    (ssize_t)(app_css_end - app_css_start));
    return ESP_OK;
}

static esp_err_t http_server_app_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)app_js_start,
                    (ssize_t)(app_js_end - app_js_start));
    return ESP_OK;
}

static esp_err_t http_server_api_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)api_js_start,
                    (ssize_t)(api_js_end - api_js_start));
    return ESP_OK;
}

static esp_err_t http_server_ble_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)ble_js_start,
                    (ssize_t)(ble_js_end - ble_js_start));
    return ESP_OK;
}

static esp_err_t http_server_kml_gpx_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)kml_gpx_js_start,
                    (ssize_t)(kml_gpx_js_end - kml_gpx_js_start));
    return ESP_OK;
}

static esp_err_t http_server_maps_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)maps_js_start,
                    (ssize_t)(maps_js_end - maps_js_start));
    return ESP_OK;
}

static esp_err_t http_server_route_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)route_js_start,
                    (ssize_t)(route_js_end - route_js_start));
    return ESP_OK;
}

static esp_err_t http_server_storage_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)storage_js_start,
                    (ssize_t)(storage_js_end - storage_js_start));
    return ESP_OK;
}

static esp_err_t http_server_jquery_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)jquery_3_3_1_min_js_start,
                    (ssize_t)(jquery_3_3_1_min_js_end - jquery_3_3_1_min_js_start));
    return ESP_OK;
}

static esp_err_t http_server_favicon_ico_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req,
                    (const char *)favicon_ico_start,
                    (ssize_t)(favicon_ico_end - favicon_ico_start));
    return ESP_OK;
}

/* ============================================================
 * API handlers
 * ============================================================ */
static esp_err_t http_server_sysinfo_handler(httpd_req_t *req)
{
    char json[768];

    int64_t up_us = esp_timer_get_time();
    uint32_t uptime_s = (uint32_t)(up_us / 1000000ULL);
    uint32_t heap_free = esp_get_free_heap_size();

    wifi_ap_record_t ap_info;
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
        rssi = ap_info.rssi;
    }

    esp_netif_ip_info_t ip_sta = {0};
    esp_netif_ip_info_t ip_ap  = {0};

    char ip_sta_str[16] = "0.0.0.0";
    char ip_ap_str[16]  = "0.0.0.0";

    if (esp_netif_sta && esp_netif_get_ip_info(esp_netif_sta, &ip_sta) == ESP_OK)
    {
        snprintf(ip_sta_str, sizeof(ip_sta_str), IPSTR, IP2STR(&ip_sta.ip));
    }

    if (esp_netif_ap && esp_netif_get_ip_info(esp_netif_ap, &ip_ap) == ESP_OK)
    {
        snprintf(ip_ap_str, sizeof(ip_ap_str), IPSTR, IP2STR(&ip_ap.ip));
    }

    bike_data_t b;
    bool bike_ok = bike_data_get(&b);

    snprintf(json, sizeof(json),
             "{"
             "\"ok\":true,"
             "\"uptime_s\":%" PRIu32 ","
             "\"heap_free\":%" PRIu32 ","
             "\"rssi\":%d,"
             "\"ip_sta\":\"%s\","
             "\"ip_ap\":\"%s\","
             "\"wifi_ssid\":\"%s\","
             "\"wifi_connected\":%s,"
             "\"local_time\":\"%s\","
             "\"bike_available\":%s,"
             "\"build\":{"
                "\"date\":\"%s\","
                "\"time\":\"%s\""
             "}"
             "}",
             uptime_s,
             heap_free,
             rssi,
             ip_sta_str,
             ip_ap_str,
             wifi_app_get_sta_ssid(),
             wifi_app_is_sta_connected() ? "true" : "false",
             sntp_time_sync_get_time(),
             bike_ok ? "true" : "false",
             __DATE__,
             __TIME__);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

static esp_err_t http_server_wifi_cfg_get_handler(httpd_req_t *req)
{
    char json[256];

    const char *ssid = wifi_app_get_sta_ssid();
    bool connected = wifi_app_is_sta_connected();

    snprintf(json, sizeof(json),
             "{"
             "\"ok\":true,"
             "\"ssid\":\"%s\","
             "\"connected\":%s"
             "}",
             ssid ? ssid : "",
             connected ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

static esp_err_t http_server_wifi_cfg_post_handler(httpd_req_t *req)
{
    char buf[256];
    int len = req->content_len;

    if (len <= 0 || len >= (int)sizeof(buf))
    {
        return http_server_send_json_error(req, "400 Bad Request",
                                           "{\"ok\":false,\"err\":\"invalid_len\"}");
    }

    int ret = httpd_req_recv(req, buf, len);
    if (ret <= 0)
    {
        return http_server_send_json_error(req, "400 Bad Request",
                                           "{\"ok\":false,\"err\":\"recv_failed\"}");
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "POST /wifi_cfg body: %s", buf);

    char ssid[33] = {0};
    char password[65] = {0};

    if (!json_get_string_value(buf, "ssid", ssid, sizeof(ssid)))
    {
        return http_server_send_json_error(req, "400 Bad Request",
                                           "{\"ok\":false,\"err\":\"missing_ssid\"}");
    }

    json_get_string_value(buf, "password", password, sizeof(password));

    if (!wifi_app_set_sta_credentials(ssid, password))
    {
        return http_server_send_json_error(req, "400 Bad Request",
                                           "{\"ok\":false,\"err\":\"invalid_credentials\"}");
    }

    if (wifi_app_send_message(WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER) != pdTRUE)
    {
        return http_server_send_json_error(req, "500 Internal Server Error",
                                           "{\"ok\":false,\"err\":\"queue_failed\"}");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"wifi_connecting\"}");
    return ESP_OK;
}

static esp_err_t http_server_wifi_reset_post_handler(httpd_req_t *req)
{
    if (wifi_app_send_message(WIFI_APP_MSG_USER_REQUESTED_STA_DISCONNECT) != pdTRUE)
    {
        return http_server_send_json_error(req, "500 Internal Server Error",
                                           "{\"ok\":false,\"err\":\"queue_failed\"}");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"wifi_cleared\"}");
    return ESP_OK;
}

/* ============================================================
 * OTA
 * ============================================================ */
esp_err_t http_server_OTA_update_handler(httpd_req_t *req)
{
    esp_ota_handle_t ota_handle = 0;

    char ota_buff[1024];
    int content_length = req->content_len;
    int content_received = 0;
    int recv_len;
    bool flash_successful = false;

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition)
    {
        ESP_LOGE(TAG, "No update partition found");
        return http_server_send_json_error(req, "500 Internal Server Error",
                                           "{\"ok\":false,\"err\":\"no_partition\"}");
    }

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return http_server_send_json_error(req, "500 Internal Server Error",
                                           "{\"ok\":false,\"err\":\"ota_begin_failed\"}");
    }

    while (content_received < content_length)
    {
        int to_read = MIN((content_length - content_received), (int)sizeof(ota_buff));
        recv_len = httpd_req_recv(req, ota_buff, to_read);

        if (recv_len < 0)
        {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
            {
                ESP_LOGW(TAG, "Socket timeout, retry");
                continue;
            }

            ESP_LOGE(TAG, "OTA recv error %d", recv_len);
            esp_ota_abort(ota_handle);
            return http_server_send_json_error(req, "400 Bad Request",
                                               "{\"ok\":false,\"err\":\"recv_failed\"}");
        }

        err = esp_ota_write(ota_handle, ota_buff, recv_len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            return http_server_send_json_error(req, "500 Internal Server Error",
                                               "{\"ok\":false,\"err\":\"ota_write_failed\"}");
        }

        content_received += recv_len;
    }

    if (esp_ota_end(ota_handle) == ESP_OK)
    {
        if (esp_ota_set_boot_partition(update_partition) == ESP_OK)
        {
            flash_successful = true;
        }
        else
        {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed");
        }
    }
    else
    {
        ESP_LOGE(TAG, "esp_ota_end failed");
    }

    if (flash_successful)
    {
        http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_SUCCESSFUL);
    }
    else
    {
        http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_FAILED);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, flash_successful ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

esp_err_t http_server_OTA_status_handler(httpd_req_t *req)
{
    char otaJSON[128];

    snprintf(otaJSON, sizeof(otaJSON),
             "{"
             "\"ota_update_status\":%d,"
             "\"compile_time\":\"%s\","
             "\"compile_date\":\"%s\""
             "}",
             g_fw_update_status, __TIME__, __DATE__);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, otaJSON, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ============================================================
 * Server configure
 * ============================================================ */
static httpd_handle_t http_server_configure(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    http_server_monitor_queue_handle = xQueueCreate(3, sizeof(http_server_queue_message_t));
    if (!http_server_monitor_queue_handle)
    {
        ESP_LOGE(TAG, "Failed to create monitor queue");
        return NULL;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        http_server_monitor,
        "http_server_monitor",
        HTTP_SERVER_MONITOR_STACK_SIZE,
        NULL,
        HTTP_SERVER_MONITOR_PRIORITY,
        &task_http_server_monitor,
        HTTP_SERVER_MONITOR_CORE_ID);

    if (task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create http_server_monitor task");
        vQueueDelete(http_server_monitor_queue_handle);
        http_server_monitor_queue_handle = NULL;
        return NULL;
    }

    config.core_id = HTTP_SERVER_TASK_CORE_ID;
    config.task_priority = HTTP_SERVER_TASK_PRIORITY;
    config.stack_size = HTTP_SERVER_TASK_STACK_SIZE;
    config.max_uri_handlers = 24;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    ESP_LOGI(TAG, "Starting server on port: '%d' with task priority: '%d'",
             config.server_port, config.task_priority);

    if (httpd_start(&http_server_handle, &config) == ESP_OK)
    {
        httpd_uri_t index_html = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = http_server_index_html_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &index_html);

        httpd_uri_t app_css = {
            .uri = "/app.css",
            .method = HTTP_GET,
            .handler = http_server_app_css_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &app_css);

        httpd_uri_t app_js = {
            .uri = "/app.js",
            .method = HTTP_GET,
            .handler = http_server_app_js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &app_js);

        httpd_uri_t api_js = {
            .uri = "/api.js",
            .method = HTTP_GET,
            .handler = http_server_api_js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &api_js);

        httpd_uri_t ble_js = {
            .uri = "/ble.js",
            .method = HTTP_GET,
            .handler = http_server_ble_js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &ble_js);

        httpd_uri_t kml_gpx_js = {
            .uri = "/kml_gpx.js",
            .method = HTTP_GET,
            .handler = http_server_kml_gpx_js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &kml_gpx_js);

        httpd_uri_t maps_js = {
            .uri = "/maps.js",
            .method = HTTP_GET,
            .handler = http_server_maps_js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &maps_js);

        httpd_uri_t route_js = {
            .uri = "/route.js",
            .method = HTTP_GET,
            .handler = http_server_route_js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &route_js);

        httpd_uri_t storage_js = {
            .uri = "/storage.js",
            .method = HTTP_GET,
            .handler = http_server_storage_js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &storage_js);

        httpd_uri_t jquery_js = {
            .uri = "/jquery-3.3.1.min.js",
            .method = HTTP_GET,
            .handler = http_server_jquery_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &jquery_js);

        httpd_uri_t favicon_ico = {
            .uri = "/favicon.ico",
            .method = HTTP_GET,
            .handler = http_server_favicon_ico_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &favicon_ico);

        httpd_uri_t ota_update = {
            .uri = "/OTAupdate",
            .method = HTTP_POST,
            .handler = http_server_OTA_update_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &ota_update);

        httpd_uri_t ota_status = {
            .uri = "/OTAstatus",
            .method = HTTP_POST,
            .handler = http_server_OTA_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &ota_status);

        httpd_uri_t sysinfo = {
            .uri = "/sysinfo",
            .method = HTTP_GET,
            .handler = http_server_sysinfo_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &sysinfo);

        httpd_uri_t wifi_cfg_get = {
            .uri = "/wifi_cfg",
            .method = HTTP_GET,
            .handler = http_server_wifi_cfg_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &wifi_cfg_get);

        httpd_uri_t wifi_cfg_post = {
            .uri = "/wifi_cfg",
            .method = HTTP_POST,
            .handler = http_server_wifi_cfg_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &wifi_cfg_post);

        httpd_uri_t wifi_reset = {
            .uri = "/wifi_reset",
            .method = HTTP_POST,
            .handler = http_server_wifi_reset_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &wifi_reset);

        ESP_LOGI(TAG, "HTTP server started");
        return http_server_handle;
    }

    ESP_LOGE(TAG, "httpd_start failed");

    if (task_http_server_monitor)
    {
        vTaskDelete(task_http_server_monitor);
        task_http_server_monitor = NULL;
    }

    if (http_server_monitor_queue_handle)
    {
        vQueueDelete(http_server_monitor_queue_handle);
        http_server_monitor_queue_handle = NULL;
    }

    return NULL;
}

/* ============================================================
 * Public API
 * ============================================================ */
void http_server_start(void)
{
    if (http_server_handle == NULL)
    {
        http_server_handle = http_server_configure();
    }
}

void http_server_stop(void)
{
    if (http_server_handle)
    {
        httpd_stop(http_server_handle);
        ESP_LOGI(TAG, "Stopping HTTP server");
        http_server_handle = NULL;
    }

    if (task_http_server_monitor)
    {
        vTaskDelete(task_http_server_monitor);
        task_http_server_monitor = NULL;
        ESP_LOGI(TAG, "Stopping HTTP server monitor");
    }

    if (http_server_monitor_queue_handle)
    {
        vQueueDelete(http_server_monitor_queue_handle);
        http_server_monitor_queue_handle = NULL;
    }
}

BaseType_t http_server_monitor_send_message(http_server_message_e msgID)
{
    if (!http_server_monitor_queue_handle)
    {
        return pdFALSE;
    }

    http_server_queue_message_t msg = {
        .msgID = msgID
    };

    return xQueueSend(http_server_monitor_queue_handle, &msg, portMAX_DELAY);
}

void http_server_fw_update_reset_callback(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Timer timed-out, restarting device");
    esp_restart();
}