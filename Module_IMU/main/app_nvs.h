#ifndef MAIN_APP_NVS_H_
#define MAIN_APP_NVS_H_

#include <stdbool.h>
#include "esp_wifi.h"

bool app_nvs_save_sta_creds(const wifi_config_t *wifi_config);
bool app_nvs_load_sta_creds(wifi_config_t *wifi_config);
bool app_nvs_clear_sta_creds(void);

#endif /* MAIN_APP_NVS_H_ */