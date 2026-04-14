/*
 * sntp_time_sync.c
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/apps/sntp.h"

#include "tasks_common.h"
#include "sntp_time_sync.h"

static const char TAG[] = "sntp_time_sync";

// Évite de lancer plusieurs fois la tâche
static bool s_task_started = false;

// Évite de reconfigurer plusieurs fois le mode SNTP
static bool s_sntp_op_mode_set = false;

static bool time_is_valid(void)
{
    time_t now = 0;
    struct tm time_info = {0};

    time(&now);
    localtime_r(&now, &time_info);

    // Si année < 2016, on considère que l'heure n'est pas encore synchronisée
    return (time_info.tm_year >= (2016 - 1900));
}

bool sntp_time_sync_is_time_set(void)
{
    return time_is_valid();
}

/**
 * Initialise le client SNTP.
 */
static void sntp_time_sync_init_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");

    if (!s_sntp_op_mode_set)
    {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        s_sntp_op_mode_set = true;
    }

    // Tu peux garder pool.ntp.org
    sntp_setservername(0, "pool.ntp.org");

    // Important: ne pas init plusieurs fois
    if (sntp_enabled())
    {
        ESP_LOGI(TAG, "SNTP already enabled");
        return;
    }

    sntp_init();

    // Fuseau France / Europe Paris
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

/**
 * Vérifie si l'heure est valide, sinon démarre SNTP.
 */
static void sntp_time_sync_obtain_time(void)
{
    if (!time_is_valid())
    {
        sntp_time_sync_init_sntp();
    }
}

/**
 * Tâche de synchronisation périodique.
 */
static void sntp_time_sync_task(void *pvParam)
{
    (void)pvParam;

    while (1)
    {
        sntp_time_sync_obtain_time();

        if (time_is_valid())
        {
            ESP_LOGI(TAG, "Time synchronized: %s", sntp_time_sync_get_time());
        }
        else
        {
            ESP_LOGW(TAG, "Time not set yet");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

char* sntp_time_sync_get_time(void)
{
    static char time_buffer[64] = {0};

    time_t now = 0;
    struct tm time_info = {0};

    time(&now);
    localtime_r(&now, &time_info);

    if (!time_is_valid())
    {
        snprintf(time_buffer, sizeof(time_buffer), "time_not_set");
    }
    else
    {
        strftime(time_buffer, sizeof(time_buffer), "%d.%m.%Y %H:%M:%S", &time_info);
    }

    return time_buffer;
}

void sntp_time_sync_task_start(void)
{
    if (s_task_started)
    {
        ESP_LOGI(TAG, "SNTP task already started");
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        sntp_time_sync_task,
        "sntp_time_sync",
        SNTP_TIME_SYNC_TASK_STACK_SIZE,
        NULL,
        SNTP_TIME_SYNC_TASK_PRIORITY,
        NULL,
        SNTP_TIME_SYNC_TASK_CORE_ID
    );

    if (ok == pdPASS)
    {
        s_task_started = true;
        ESP_LOGI(TAG, "SNTP task started");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start SNTP task");
    }
}