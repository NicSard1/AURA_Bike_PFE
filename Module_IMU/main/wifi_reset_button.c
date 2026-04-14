#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "tasks_common.h"
#include "wifi_app.h"
#include "wifi_reset_button.h"

static const char TAG[] = "wifi_reset_button";

static SemaphoreHandle_t wifi_reset_semaphore = NULL;

void IRAM_ATTR wifi_reset_button_isr_handler(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(wifi_reset_semaphore, &higher_priority_task_woken);
    if (higher_priority_task_woken)
    {
        portYIELD_FROM_ISR();
    }
}

static void wifi_reset_button_task(void *pvParam)
{
    (void)pvParam;

    for (;;)
    {
        if (xSemaphoreTake(wifi_reset_semaphore, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "BOOT button pressed -> clear WiFi credentials");
            wifi_app_send_message(WIFI_APP_MSG_USER_REQUESTED_STA_DISCONNECT);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void wifi_reset_button_config(void)
{
    wifi_reset_semaphore = xSemaphoreCreateBinary();
    if (!wifi_reset_semaphore)
    {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    gpio_reset_pin(WIFI_RESET_BUTTON);
    gpio_set_direction(WIFI_RESET_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(WIFI_RESET_BUTTON, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(WIFI_RESET_BUTTON, GPIO_INTR_NEGEDGE);

    xTaskCreatePinnedToCore(
        wifi_reset_button_task,
        "wifi_reset_button",
        WIFI_RESET_BUTTON_TASK_STACK_SIZE,
        NULL,
        WIFI_RESET_BUTTON_TASK_PRIORITY,
        NULL,
        WIFI_RESET_BUTTON_TASK_CORE_ID
    );

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(WIFI_RESET_BUTTON, wifi_reset_button_isr_handler, NULL);
}