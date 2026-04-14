#include <string.h>
#include <stdlib.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "lwip/netdb.h"
#include "lwip/inet.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "wifi_app.h"
#include "http_server.h"
#include "sntp_time_sync.h"
#include "app_nvs.h"
#include "aws_mqtt.h"

static const char TAG[] = "wifi_app";

static QueueHandle_t wifi_app_queue_handle = NULL;
static EventGroupHandle_t wifi_app_event_group = NULL;

esp_netif_t *esp_netif_sta = NULL;
esp_netif_t *esp_netif_ap  = NULL;

static int s_retry_num = 0;
static wifi_config_t *s_wifi_config = NULL;
static bool s_last_connect_from_http = false;

EventGroupHandle_t wifi_app_get_event_group(void)
{
    return wifi_app_event_group;
}

wifi_config_t *wifi_app_get_wifi_config(void)
{
    return s_wifi_config;
}

bool wifi_app_is_sta_connected(void)
{
    if (!wifi_app_event_group)
    {
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(wifi_app_event_group);
    return (bits & WIFI_APP_STA_CONNECTED_BIT) != 0;
}

const char *wifi_app_get_sta_ssid(void)
{
    if (!s_wifi_config)
    {
        return "";
    }

    return (const char *)s_wifi_config->sta.ssid;
}

bool wifi_app_set_sta_credentials(const char *ssid, const char *password)
{
    if (!s_wifi_config || !ssid || !password)
    {
        return false;
    }

    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);

    if (ssid_len == 0 || ssid_len > MAX_SSID_LENGTH)
    {
        return false;
    }

    if (pass_len > MAX_PASSWORD_LENGTH)
    {
        return false;
    }

    memset(s_wifi_config, 0, sizeof(wifi_config_t));

    strncpy((char *)s_wifi_config->sta.ssid, ssid, sizeof(s_wifi_config->sta.ssid) - 1);
    strncpy((char *)s_wifi_config->sta.password, password, sizeof(s_wifi_config->sta.password) - 1);

    s_wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    s_wifi_config->sta.pmf_cfg.capable = true;
    s_wifi_config->sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "New STA credentials set: ssid='%s'", ssid);
    return true;
}

static void wifi_app_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
                break;

            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");
                break;

            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Station connected to ESP32 AP");
                break;

            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "Station disconnected from ESP32 AP");
                break;

            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WIFI_EVENT_STA_START -> connecting...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
            {
                wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED, reason=%d", disc ? disc->reason : -1);

                xEventGroupClearBits(wifi_app_event_group, WIFI_APP_STA_CONNECTED_BIT);

                /* on coupe MQTT dès perte réseau */
                aws_mqtt_stop();

                if (s_retry_num < MAX_CONNECTION_RETRIES)
                {
                    s_retry_num++;
                    ESP_LOGW(TAG, "Retry to connect to AP (%d/%d)", s_retry_num, MAX_CONNECTION_RETRIES);
                    esp_wifi_connect();
                }
                else
                {
                    xEventGroupSetBits(wifi_app_event_group, WIFI_APP_STA_FAIL_BIT);
                    ESP_LOGE(TAG, "Failed to connect to router WiFi after retries");
                    wifi_app_send_message(WIFI_APP_MSG_STA_DISCONNECTED);
                }
                break;
            }

            default:
                break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
            case IP_EVENT_STA_GOT_IP:
            {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: " IPSTR, IP2STR(&event->ip_info.ip));

                s_retry_num = 0;
                xEventGroupClearBits(wifi_app_event_group, WIFI_APP_STA_FAIL_BIT);
                xEventGroupSetBits(wifi_app_event_group, WIFI_APP_STA_CONNECTED_BIT);

                sntp_time_sync_task_start();
                wifi_app_send_message(WIFI_APP_MSG_STA_CONNECTED_GOT_IP);
                break;
            }

            default:
                break;
        }
    }
}

static void wifi_app_event_handler_init(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_event_handler_instance_t instance_wifi_event;
    esp_event_handler_instance_t instance_ip_event;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_app_event_handler, NULL, &instance_wifi_event));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &wifi_app_event_handler, NULL, &instance_ip_event));
}

static void wifi_app_default_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    esp_netif_sta = esp_netif_create_default_wifi_sta();
    esp_netif_ap  = esp_netif_create_default_wifi_ap();
}

static void wifi_app_soft_ap_config(void)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .password = WIFI_AP_PASSWORD,
            .channel = WIFI_AP_CHANNEL,
            .ssid_hidden = WIFI_AP_SSID_HIDDEN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = WIFI_AP_MAX_CONNECTIONS,
            .beacon_interval = WIFI_AP_BEACON_INTERVAL,
        },
    };

    if (strlen(WIFI_AP_PASSWORD) == 0)
    {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_netif_ip_info_t ap_ip_info;
    memset(&ap_ip_info, 0, sizeof(ap_ip_info));

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(esp_netif_ap));
    inet_pton(AF_INET, WIFI_AP_IP, &ap_ip_info.ip);
    inet_pton(AF_INET, WIFI_AP_GATEWAY, &ap_ip_info.gw);
    inet_pton(AF_INET, WIFI_AP_NETMASK, &ap_ip_info.netmask);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ap_ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_AP_BANDWIDTH));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_STA_POWER_SAVE));
}

static void wifi_app_load_default_sta_config(void)
{
    memset(s_wifi_config, 0, sizeof(wifi_config_t));

    strncpy((char *)s_wifi_config->sta.ssid, WIFI_STA_SSID, sizeof(s_wifi_config->sta.ssid) - 1);
    strncpy((char *)s_wifi_config->sta.password, WIFI_STA_PASSWORD, sizeof(s_wifi_config->sta.password) - 1);

    s_wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    s_wifi_config->sta.pmf_cfg.capable = true;
    s_wifi_config->sta.pmf_cfg.required = false;
}

static void wifi_app_connect_sta(void)
{
    if (!s_wifi_config)
    {
        return;
    }

    ESP_LOGI(TAG, "Applying STA config: ssid='%s'", (char *)s_wifi_config->sta.ssid);

    xEventGroupClearBits(wifi_app_event_group, WIFI_APP_STA_CONNECTED_BIT | WIFI_APP_STA_FAIL_BIT);
    s_retry_num = 0;

    /* Stop MQTT proprement avant changement réseau */
    aws_mqtt_stop();

    /*
     * Déconnexion préalable : on ne CHECK pas ici, car si déjà déconnecté
     * ou en transition, ESP-IDF peut renvoyer un état non bloquant.
     */
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT && err != ESP_ERR_WIFI_STATE)
    {
        ESP_LOGW(TAG, "esp_wifi_disconnect returned %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(300));

    err = esp_wifi_set_config(WIFI_IF_STA, s_wifi_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE)
    {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Trying STA connect to '%s'", (char *)s_wifi_config->sta.ssid);
}

static void wifi_app_task(void *pvParameters)
{
    (void)pvParameters;
    wifi_app_queue_message_t msg;

    wifi_app_event_group = xEventGroupCreate();
    if (!wifi_app_event_group)
    {
        ESP_LOGE(TAG, "Failed to create wifi_app_event_group");
        vTaskDelete(NULL);
        return;
    }

    wifi_app_event_handler_init();
    wifi_app_default_wifi_init();
    wifi_app_soft_ap_config();

    if (!app_nvs_load_sta_creds(s_wifi_config))
    {
        ESP_LOGW(TAG, "No saved WiFi credentials, using fallback credentials");
        wifi_app_load_default_sta_config();
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, s_wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi started successfully (AP+STA)");

    wifi_app_send_message(WIFI_APP_MSG_START_HTTP_SERVER);

    for (;;)
    {
        if (xQueueReceive(wifi_app_queue_handle, &msg, portMAX_DELAY))
        {
            switch (msg.msgID)
            {
                case WIFI_APP_MSG_START_HTTP_SERVER:
                    ESP_LOGI(TAG, "WIFI_APP_MSG_START_HTTP_SERVER");
                    http_server_start();
                    ESP_LOGI(TAG, "HTTP server started (AP: http://%s/)", WIFI_AP_IP);
                    break;

                case WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER:
                    ESP_LOGI(TAG, "WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER");
                    s_last_connect_from_http = true;
                    wifi_app_connect_sta();
                    break;

                case WIFI_APP_MSG_STA_CONNECTED_GOT_IP:
                    ESP_LOGI(TAG, "STA connected: Internet OK -> AWS MQTT can start");

                    if (s_last_connect_from_http)
                    {
                        app_nvs_save_sta_creds(s_wifi_config);
                        s_last_connect_from_http = false;
                        ESP_LOGI(TAG, "New WiFi credentials saved to NVS");
                    }

                    /* démarrage MQTT événementiel */
                    aws_mqtt_start_if_needed();
                    break;

                case WIFI_APP_MSG_USER_REQUESTED_STA_DISCONNECT:
                    ESP_LOGW(TAG, "User requested STA disconnect + clear saved credentials");

                    app_nvs_clear_sta_creds();
                    s_last_connect_from_http = false;
                    s_retry_num = MAX_CONNECTION_RETRIES;

                    xEventGroupClearBits(wifi_app_event_group,
                                         WIFI_APP_STA_CONNECTED_BIT | WIFI_APP_STA_FAIL_BIT);

                    aws_mqtt_stop();

                    {
                        esp_err_t err = esp_wifi_disconnect();
                        if (err != ESP_OK &&
                            err != ESP_ERR_WIFI_NOT_CONNECT &&
                            err != ESP_ERR_WIFI_STATE)
                        {
                            ESP_LOGW(TAG, "esp_wifi_disconnect returned %s", esp_err_to_name(err));
                        }
                    }
                    break;

                case WIFI_APP_MSG_LOAD_SAVED_CREDENTIALS:
                    break;

                case WIFI_APP_MSG_STA_DISCONNECTED:
                    ESP_LOGW(TAG, "STA disconnected: Internet lost");
                    break;

                default:
                    break;
            }
        }
    }
}

BaseType_t wifi_app_send_message(wifi_app_message_e msgID)
{
    if (!wifi_app_queue_handle)
    {
        return pdFALSE;
    }

    wifi_app_queue_message_t msg = { .msgID = msgID };
    return xQueueSend(wifi_app_queue_handle, &msg, portMAX_DELAY);
}

void wifi_app_start(void)
{
    ESP_LOGI(TAG, "STARTING WIFI APPLICATION");

    esp_log_level_set("wifi", ESP_LOG_NONE);

    s_wifi_config = malloc(sizeof(wifi_config_t));
    if (!s_wifi_config)
    {
        ESP_LOGE(TAG, "Failed to allocate wifi config");
        return;
    }
    memset(s_wifi_config, 0, sizeof(wifi_config_t));

    wifi_app_queue_handle = xQueueCreate(8, sizeof(wifi_app_queue_message_t));
    if (!wifi_app_queue_handle)
    {
        ESP_LOGE(TAG, "Failed to create wifi_app_queue_handle");
        free(s_wifi_config);
        s_wifi_config = NULL;
        return;
    }

    xTaskCreatePinnedToCore(
        wifi_app_task,
        "wifi_app_task",
        4096,
        NULL,
        5,
        NULL,
        0);
}