#include "gnss_link.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "gnss_link"
#define RX_BUF_SZ 512

static SemaphoreHandle_t s_mtx;
static gnss_link_data_t s_data;
static bool s_ready = false;

static bool parse_line(const char *s, gnss_link_data_t *d)
{
    if (!s || !d) {
        return false;
    }

    memset(d, 0, sizeof(*d));

    /* Valeurs par défaut */
    d->fix_ok = false;
    d->sats = 0;
    d->speed_kmh = 0.0f;
    d->dist_m = 0.0f;
    d->alt_m = 0.0f;
    d->have_baro_tx = false;
    d->baro_pa = 0;

    char buf[256];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(buf, ";", &saveptr);

    bool have_spd = false;
    bool have_dst = false;

    while (tok) {
        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            const char *key = tok;
            const char *val = eq + 1;

            if (strcmp(key, "SPD") == 0) {
                d->speed_kmh = strtof(val, NULL);
                have_spd = true;
            } else if (strcmp(key, "DST") == 0) {
                d->dist_m = strtof(val, NULL);
                have_dst = true;
            } else if (strcmp(key, "ALT") == 0) {
                d->alt_m = strtof(val, NULL);
            } else if (strcmp(key, "FIX") == 0) {
                d->fix_ok = (atoi(val) == 1);
            } else if (strcmp(key, "SAT") == 0) {
                d->sats = atoi(val);
            } else if (strcmp(key, "BARO") == 0) {
                int32_t p = (int32_t)strtol(val, NULL, 10);
                if (p > 0) {
                    d->have_baro_tx = true;
                    d->baro_pa = p;
                }
            }
        }

        tok = strtok_r(NULL, ";", &saveptr);
    }

    if (!have_spd && !have_dst) {
        return false;
    }

    d->tick_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    return true;
}

static void rx_task(void *arg)
{
    const int uart_num = (int)(intptr_t)arg;
    static uint8_t rx[RX_BUF_SZ];
    static char line[256];
    int line_len = 0;

    while (1) {
        int len = uart_read_bytes(uart_num, rx, sizeof(rx), pdMS_TO_TICKS(200));
        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            char c = (char)rx[i];

            if (c == '\n') {
                line[line_len] = 0;

                gnss_link_data_t tmp = {0};
                if (parse_line(line, &tmp)) {
                    xSemaphoreTake(s_mtx, portMAX_DELAY);
                    s_data = tmp;
                    s_ready = true;
                    xSemaphoreGive(s_mtx);

                    ESP_LOGI(TAG,
                             "RX GNSS: fix=%d sats=%d spd=%.2f dst=%.1f alt=%.2f baro=%s",
                             tmp.fix_ok,
                             tmp.sats,
                             tmp.speed_kmh,
                             tmp.dist_m,
                             tmp.alt_m,
                             tmp.have_baro_tx ? "yes" : "no");
                }

                line_len = 0;
            } else if (c != '\r') {
                if (line_len < (int)sizeof(line) - 1) {
                    line[line_len++] = c;
                } else {
                    line_len = 0;
                }
            }
        }
    }
}

void gnss_link_start(int uart_num, int tx_pin, int rx_pin, int baud)
{
    s_mtx = xSemaphoreCreateMutex();

    uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    ESP_ERROR_CHECK(uart_param_config(uart_num, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, 2048, 0, 0, NULL, 0));

    xTaskCreate(rx_task, "gnss_rx", 4096, (void*)(intptr_t)uart_num, 6, NULL);

    ESP_LOGI(TAG, "GNSS link started on UART%d (TX=%d RX=%d @%d)",
             uart_num, tx_pin, rx_pin, baud);
}

bool gnss_link_get(gnss_link_data_t *out)
{
    if (!out || !s_ready) return false;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_mtx);

    return true;
}