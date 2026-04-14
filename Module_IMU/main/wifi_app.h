#ifndef MAIN_WIFI_APP_H_
#define MAIN_WIFI_APP_H_

#include <stdbool.h>
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// ==============================
// SOFTAP
// ==============================
#define WIFI_AP_SSID               "Aura_bike"
#define WIFI_AP_PASSWORD           "12345678"
#define WIFI_AP_CHANNEL            1
#define WIFI_AP_SSID_HIDDEN        0
#define WIFI_AP_MAX_CONNECTIONS    4
#define WIFI_AP_BEACON_INTERVAL    100

#define WIFI_AP_IP                 "192.168.4.1"
#define WIFI_AP_GATEWAY            "192.168.4.1"
#define WIFI_AP_NETMASK            "255.255.255.0"

#define WIFI_AP_BANDWIDTH          WIFI_BW_HT20

// ==============================
// STA default fallback
// ==============================
#define WIFI_STA_SSID              "Tabarnak"
#define WIFI_STA_PASSWORD          "sandre29"

#define WIFI_STA_POWER_SAVE        WIFI_PS_NONE
#define MAX_CONNECTION_RETRIES     10

#define MAX_SSID_LENGTH            32
#define MAX_PASSWORD_LENGTH        64

// EventGroup bits
#define WIFI_APP_STA_CONNECTED_BIT   BIT0
#define WIFI_APP_STA_FAIL_BIT        BIT1

extern esp_netif_t *esp_netif_sta;
extern esp_netif_t *esp_netif_ap;

EventGroupHandle_t wifi_app_get_event_group(void);

// Messages queue
typedef enum wifi_app_message
{
    WIFI_APP_MSG_START_HTTP_SERVER = 0,
    WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER,
    WIFI_APP_MSG_STA_CONNECTED_GOT_IP,
    WIFI_APP_MSG_USER_REQUESTED_STA_DISCONNECT,
    WIFI_APP_MSG_LOAD_SAVED_CREDENTIALS,
    WIFI_APP_MSG_STA_DISCONNECTED,
} wifi_app_message_e;

typedef struct wifi_app_queue_message
{
    wifi_app_message_e msgID;
} wifi_app_queue_message_t;

BaseType_t wifi_app_send_message(wifi_app_message_e msgID);
void wifi_app_start(void);

// helpers
wifi_config_t *wifi_app_get_wifi_config(void);
bool wifi_app_set_sta_credentials(const char *ssid, const char *password);
const char *wifi_app_get_sta_ssid(void);
bool wifi_app_is_sta_connected(void);

#endif /* MAIN_WIFI_APP_H_ */