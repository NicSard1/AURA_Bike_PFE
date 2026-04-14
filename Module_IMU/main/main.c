#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

#include "gy86.h"
#include "bike_data.h"
#include "lcd_lvgl.h"

#include "wifi_app.h"
#include "wifi_reset_button.h"

static void ui_feed_task(void* arg)
{
    (void)arg;

    while (1) {
        bike_data_t b;
        if (bike_data_get(&b)) {
            ui_update_from_bike(&b);
        }
        vTaskDelay(pdMS_TO_TICKS(200)); // 5 Hz
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Capteurs
    gy86_start();

    // UI
    lcd_lvgl_start();

    // Fusion GY86 + GNSS (UART)
    bike_data_start();

    // WiFi + HTTP
    wifi_app_start();
    wifi_reset_button_config();

    // Feed UI
    xTaskCreate(ui_feed_task, "ui_feed", 4096, NULL, 5, NULL);
}