#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Données consolidées pour l'UI */
typedef struct
{
    /* Cap */
    bool  have_heading;
    float heading_deg;

    /* Temp / baro */
    bool  have_baro;
    float altitude_m;
    float denivele_plus_m;
    float temp_c;

    /* GNSS */
    bool  have_speed;
    float speed_kmh;

    bool  have_distance;
    float distance_m;

    bool  have_gnss_alt;
    float gnss_alt_m;

    bool  gnss_fix_ok;
    int   gnss_sats;

    /* IMU / sécurité */
    bool  have_imu;
    float longitudinal_accel_g;   /* acc avant/arrière filtrée */
    float bike_tilt_deg;          /* inclinaison estimée depuis gravité */
    bool  braking_detected;
    bool  fall_detected;

    /* Pente */
    bool  have_grade;
    float grade_pct;

    /* Meta */
    uint32_t tick_ms;
} bike_data_t;

void bike_data_start(void);
bool bike_data_get(bike_data_t *out);

#ifdef __cplusplus
}
#endif