#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "app_nvs.h"

static const char TAG[] = "app_nvs";

#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

bool app_nvs_save_sta_creds(const wifi_config_t *wifi_config)
{
    if (!wifi_config) return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, (const char *)wifi_config->sta.ssid);
    if (err == ESP_OK)
    {
        err = nvs_set_str(handle, NVS_KEY_PASS, (const char *)wifi_config->sta.password);
    }

    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "save creds failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "STA credentials saved");
    return true;
}

bool app_nvs_load_sta_creds(wifi_config_t *wifi_config)
{
    if (!wifi_config) return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return false;
    }

    char ssid_tmp[33] = {0};
    char pass_tmp[65] = {0};

    size_t ssid_len = sizeof(ssid_tmp);
    size_t pass_len = sizeof(pass_tmp);

    err = nvs_get_str(handle, NVS_KEY_SSID, ssid_tmp, &ssid_len);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return false;
    }

    err = nvs_get_str(handle, NVS_KEY_PASS, pass_tmp, &pass_len);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);

    memset(wifi_config, 0, sizeof(wifi_config_t));

    strncpy((char *)wifi_config->sta.ssid, ssid_tmp, sizeof(wifi_config->sta.ssid) - 1);
    strncpy((char *)wifi_config->sta.password, pass_tmp, sizeof(wifi_config->sta.password) - 1);

    wifi_config->sta.ssid[sizeof(wifi_config->sta.ssid) - 1] = '\0';
    wifi_config->sta.password[sizeof(wifi_config->sta.password) - 1] = '\0';

    wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config->sta.pmf_cfg.capable = true;
    wifi_config->sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "STA credentials loaded: ssid='%s'", (char *)wifi_config->sta.ssid);
    return true;
}

bool app_nvs_clear_sta_creds(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_erase_key(handle, NVS_KEY_SSID);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        return false;
    }

    err = nvs_erase_key(handle, NVS_KEY_PASS);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "clear creds failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "STA credentials cleared");
    return true;
}