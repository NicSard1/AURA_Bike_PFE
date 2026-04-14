#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool  fix_ok;
    int   sats;
    float speed_kmh;
    float dist_m;
    float alt_m;

    /* Optionnel : transmis par la carte 4G LTE si un baro existe là-bas */
    bool  have_baro_tx;
    int32_t baro_pa;

    uint32_t tick_ms;
} gnss_link_data_t;

void gnss_link_start(int uart_num, int tx_pin, int rx_pin, int baud);
bool gnss_link_get(gnss_link_data_t *out);