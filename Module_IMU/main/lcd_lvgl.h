#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "bike_data.h"

/* Démarre LCD + LVGL + UI */
void lcd_lvgl_start(void);

/* Luminosité 0..100% */
void lcd_set_brightness(uint8_t percent);

/* Update UI depuis bike_data (thread-safe via mutex LVGL) */
void ui_update_from_bike(const bike_data_t* b);

#ifdef __cplusplus
}
#endif