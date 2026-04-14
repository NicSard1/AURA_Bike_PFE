#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_err.h"
#include "mqtt_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_app.h"
#include "sntp_time_sync.h"
#include "aws_mqtt.h"

#define TAG "AWS_MQTT"

#define AWS_IOT_ENDPOINT "a1s7xf2mftvra8-ats.iot.eu-west-3.amazonaws.com"
#define AWS_IOT_PORT     8883
#define AWS_TOPIC_PUB    "aura_bike/status"

extern const uint8_t AmazonRootCA1_pem_start[] asm("_binary_AmazonRootCA1_pem_start");
extern const uint8_t AmazonRootCA1_pem_end[]   asm("_binary_AmazonRootCA1_pem_end");

extern const uint8_t device_certificate_pem_crt_start[] asm("_binary_device_certificate_pem_crt_start");
extern const uint8_t device_certificate_pem_crt_end[]   asm("_binary_device_certificate_pem_crt_end");

extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");
extern const uint8_t private_pem_key_end[]   asm("_binary_private_pem_key_end");

static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_mqtt_connected = false;
static volatile bool s_mqtt_started = false;
static volatile bool s_mqtt_initialized = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_id;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (!event) return;

    switch (event->event_id)
    {
        case MQTT_EVENT_CONNECTED:
            s_mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT connected");
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            break;

        case MQTT_EVENT_ERROR:
            s_mqtt_connected = false;
            ESP_LOGE(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

void aws_mqtt_init(void)
{
    if (s_mqtt_initialized) return;

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = "mqtts://" AWS_IOT_ENDPOINT,
        .broker.address.port = AWS_IOT_PORT,
        .broker.verification.certificate = (const char *)AmazonRootCA1_pem_start,
        .credentials.authentication.certificate = (const char *)device_certificate_pem_crt_start,
        .credentials.authentication.key = (const char *)private_pem_key_start,
        .session.keepalive = 120,
        .network.reconnect_timeout_ms = 5000,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client)
    {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "register_event failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return;
    }

    s_mqtt_initialized = true;
    ESP_LOGI(TAG, "MQTT initialized");
}

void aws_mqtt_start_if_needed(void)
{
    if (!wifi_app_is_sta_connected()) return;
    if (!sntp_time_sync_is_time_set()) return;

    if (!s_mqtt_initialized)
    {
        aws_mqtt_init();
    }

    if (!s_client) return;

    if (!s_mqtt_started)
    {
        esp_err_t err = esp_mqtt_client_start(s_client);
        if (err == ESP_OK)
        {
            s_mqtt_started = true;
            ESP_LOGI(TAG, "MQTT started");
        }
        else
        {
            ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
        }
    }
}

void aws_mqtt_stop(void)
{
    if (!s_client)
    {
        s_mqtt_connected = false;
        s_mqtt_started = false;
        return;
    }

    if (s_mqtt_started)
    {
        esp_mqtt_client_stop(s_client);
    }

    s_mqtt_connected = false;
    s_mqtt_started = false;
}

bool aws_mqtt_is_connected(void)
{
    return s_mqtt_connected;
}

void aws_mqtt_task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        if (wifi_app_is_sta_connected())
        {
            aws_mqtt_start_if_needed();

            if (s_client && s_mqtt_connected)
            {
                const char *payload = "{\"device\":\"AURA Bike\",\"status\":\"online\"}";
                int msg_id = esp_mqtt_client_publish(s_client, AWS_TOPIC_PUB, payload, 0, 1, 0);
                ESP_LOGI(TAG, "PUB id=%d", msg_id);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}