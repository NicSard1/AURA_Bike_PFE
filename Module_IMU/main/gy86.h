#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    /* IMU */
    bool  have_imu;
    float acc_x, acc_y, acc_z;
    float gyro_x, gyro_y, gyro_z;
    float imu_temp_c;

    /* MAG */
    bool  have_mag;
    float mag_x_mg, mag_y_mg, mag_z_mg;
    float heading_deg;

    /* BARO */
    bool    have_baro;
    int32_t pressure_pa;
    float   baro_temp_c;

    float   altitude_m;          // altitude baro brute MSL
    float   altitude_filt_m;     // altitude baro filtrée pour affichage / fusion
    float   denivele_plus_m;     // D+ cumulé depuis le baro

    uint32_t tick_ms;
} gy86_snapshot_t;

void gy86_start(void);
bool gy86_get_snapshot(gy86_snapshot_t *out);

#ifdef __cplusplus
}
#endif