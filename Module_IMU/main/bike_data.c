#include "bike_data.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "driver/uart.h"

#include "gy86.h"
#include "gnss_link.h"

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define TAG "bike_data"

#define GNSS_UART_NUM   UART_NUM_1
#define GNSS_TX_PIN     5
#define GNSS_RX_PIN     6
#define GNSS_BAUD       115200

#define BIKE_FUSION_PERIOD_MS     200

/* ========================= Fusion altitude ========================= */

#define GNSS_MIN_SATS             5
#define GNSS_ALT_CORR_ALPHA       0.02f

/* ========================= Pente ========================= */

#define GRADE_MIN_DIST_STEP_M     3.0f
#define GRADE_MIN_SPEED_KMH       3.0f
#define GRADE_MAX_ABS_PCT         35.0f

/* ========================= Détection freinage =========================
 *
 * IMPORTANT :
 * Vérifie l’unité réelle de l’accéléro MPU6050 dans ta lib.
 * - si au repos Z ~ +1.0  -> laisser ACC_1G_REF = 1.0f
 * - si au repos Z ~ +9.81 -> mettre ACC_1G_REF = 9.81f
 */
#define ACC_1G_REF                1.0f

#define BRAKE_ACC_ALPHA           0.20f
#define BRAKE_THRESH_G            0.18f
#define BRAKE_RELEASE_G           0.08f
#define BRAKE_MIN_SPEED_KMH       4.0f
#define BRAKE_CONFIRM_SAMPLES     2
#define BRAKE_MIN_GNSS_DECEL      1.5f   /* km/h/s */

/* ========================= Détection chute ========================= */

#define FALL_EVENT_G_HIGH         1.80f
#define FALL_EVENT_G_LOW          0.35f
#define FALL_EVENT_GYRO_DPS       180.0f

#define FALL_TILT_CONFIRM_DEG     60.0f
#define FALL_STILL_G_DELTA        0.12f
#define FALL_STILL_GYRO_DPS       20.0f
#define FALL_CONFIRM_MS           1500u
#define FALL_LATCH_MS             5000u
#define FALL_CANDIDATE_TIMEOUT_MS 3000u
#define FALL_MAX_SPEED_CONFIRM_KMH 3.0f

#ifndef PI_F
#define PI_F 3.14159265358979323846f
#endif

/* ========================= Etat global ========================= */

static bike_data_t s_bike;
static SemaphoreHandle_t s_mtx = NULL;
static bool s_ready = false;

/* Fusion altitude */
static bool  s_alt_fusion_init = false;
static float s_baro_gnss_offset_m = 0.0f;

/* Pente */
static bool  s_grade_init = false;
static float s_last_dist_m = 0.0f;
static float s_last_alt_m = 0.0f;

/* Freinage */
static bool  s_brake_init = false;
static float s_long_accel_filt_g = 0.0f;
static int   s_brake_confirm_count = 0;
static bool  s_braking = false;

static bool     s_speed_hist_init = false;
static float    s_prev_speed_kmh = 0.0f;
static uint32_t s_prev_speed_tick_ms = 0;

/* Chute */
static bool     s_fall_candidate = false;
static uint32_t s_fall_candidate_tick_ms = 0;
static uint32_t s_fall_latched_until_ms = 0;
static float    s_last_tilt_deg = 0.0f;

/* ========================= Helpers ========================= */

static inline void lock(void)
{
    if (s_mtx)
    {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
    }
}

static inline void unlock(void)
{
    if (s_mtx)
    {
        xSemaphoreGive(s_mtx);
    }
}

static inline float clampf_local(float x, float xmin, float xmax)
{
    if (x < xmin) return xmin;
    if (x > xmax) return xmax;
    return x;
}

static inline float norm3f_local(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

static inline float max3f_local(float a, float b, float c)
{
    float m = (a > b) ? a : b;
    return (c > m) ? c : m;
}

static inline bool tick_after_or_equal(uint32_t a, uint32_t b)
{
    return ((int32_t)(a - b) >= 0);
}

/* ========================= Fusion principale ========================= */

static void fuse_once(void)
{
    gy86_snapshot_t gy = {0};
    gnss_link_data_t gn = {0};

    const bool ok_gy = gy86_get_snapshot(&gy);
    const bool ok_gn = gnss_link_get(&gn);

    bike_data_t out;
    memset(&out, 0, sizeof(out));

    float baro_alt = 0.0f;
    bool have_baro_alt = false;

    /* ==================== Données GY86 ==================== */
    if (ok_gy)
    {
        out.have_heading = gy.have_mag;
        out.heading_deg  = gy.heading_deg;

        out.have_baro = gy.have_baro;
        out.temp_c = gy.have_baro ? gy.baro_temp_c : gy.imu_temp_c;
        out.denivele_plus_m = gy.denivele_plus_m;

        out.have_imu = gy.have_imu;

        if (gy.have_baro)
        {
            baro_alt = isfinite(gy.altitude_filt_m) ? gy.altitude_filt_m : gy.altitude_m;
            have_baro_alt = isfinite(baro_alt);
        }

        out.tick_ms = gy.tick_ms;
    }

    /* ==================== Données GNSS ==================== */
    if (ok_gn)
    {
        out.gnss_fix_ok = gn.fix_ok;
        out.gnss_sats = gn.sats;

        out.have_speed = gn.fix_ok && isfinite(gn.speed_kmh);
        out.speed_kmh = out.have_speed ? gn.speed_kmh : 0.0f;

        out.have_distance = gn.fix_ok && isfinite(gn.dist_m);
        out.distance_m = out.have_distance ? gn.dist_m : 0.0f;

        out.have_gnss_alt = gn.fix_ok && isfinite(gn.alt_m);
        out.gnss_alt_m = out.have_gnss_alt ? gn.alt_m : 0.0f;

        if (!ok_gy || tick_after_or_equal(gn.tick_ms, out.tick_ms))
        {
            out.tick_ms = gn.tick_ms;
        }
    }

    const bool gnss_alt_usable =
        ok_gn &&
        gn.fix_ok &&
        gn.sats >= GNSS_MIN_SATS &&
        isfinite(gn.alt_m);

    /* ==================== Altitude fusionnée ==================== */
    if (have_baro_alt)
    {
        float fused_alt = baro_alt;

        if (gnss_alt_usable)
        {
            if (!s_alt_fusion_init)
            {
                s_alt_fusion_init = true;
                s_baro_gnss_offset_m = gn.alt_m - baro_alt;
            }
            else
            {
                float target_offset = gn.alt_m - baro_alt;
                s_baro_gnss_offset_m += GNSS_ALT_CORR_ALPHA * (target_offset - s_baro_gnss_offset_m);
            }

            fused_alt = baro_alt + s_baro_gnss_offset_m;
        }

        out.altitude_m = isfinite(fused_alt) ? fused_alt : 0.0f;
    }
    else if (gnss_alt_usable)
    {
        out.altitude_m = gn.alt_m;
    }
    else
    {
        out.altitude_m = 0.0f;
    }

    /* ==================== Pente ==================== */
    out.have_grade = false;
    out.grade_pct = 0.0f;

    if (out.have_distance && isfinite(out.altitude_m))
    {
        if (!s_grade_init)
        {
            s_grade_init = true;
            s_last_dist_m = out.distance_m;
            s_last_alt_m = out.altitude_m;
        }
        else
        {
            float dd = out.distance_m - s_last_dist_m;
            float da = out.altitude_m - s_last_alt_m;

            if (out.have_speed &&
                out.speed_kmh >= GRADE_MIN_SPEED_KMH &&
                fabsf(dd) >= GRADE_MIN_DIST_STEP_M)
            {
                float grade = (da / dd) * 100.0f;

                if (isfinite(grade) && fabsf(grade) <= GRADE_MAX_ABS_PCT)
                {
                    out.have_grade = true;
                    out.grade_pct = grade;
                }

                s_last_dist_m = out.distance_m;
                s_last_alt_m = out.altitude_m;
            }
        }
    }

    /* ==================== IMU : freinage + chute ==================== */
    out.longitudinal_accel_g = 0.0f;
    out.bike_tilt_deg = s_last_tilt_deg;
    out.braking_detected = false;
    out.fall_detected = false;

    if (ok_gy && gy.have_imu)
    {
        const float ax = gy.acc_x;
        const float ay = gy.acc_y;
        const float az = gy.acc_z;

        const float gx = gy.gyro_x;
        const float gyy = gy.gyro_y;
        const float gz = gy.gyro_z;

        const float acc_norm = norm3f_local(ax, ay, az);
        const float gyro_abs_max = max3f_local(fabsf(gx), fabsf(gyy), fabsf(gz));

        /* ===== Inclinaison vélo =====
         * 0 deg = vélo droit
         * 90 deg = vélo quasi couché
         */
        if (isfinite(acc_norm) && acc_norm > 0.001f)
        {
            float c = clampf_local(az / acc_norm, -1.0f, 1.0f);
            s_last_tilt_deg = acosf(c) * (180.0f / PI_F);
        }
        out.bike_tilt_deg = s_last_tilt_deg;

        /* ===== Freinage ===== */
        if (!s_brake_init)
        {
            s_brake_init = true;
            s_long_accel_filt_g = ax;
        }
        else
        {
            s_long_accel_filt_g += BRAKE_ACC_ALPHA * (ax - s_long_accel_filt_g);
        }

        out.longitudinal_accel_g = s_long_accel_filt_g;

        float gnss_decel_kmh_s = 0.0f;
        bool gnss_decel_ok = false;

        if (out.have_speed)
        {
            if (!s_speed_hist_init)
            {
                s_speed_hist_init = true;
                s_prev_speed_kmh = out.speed_kmh;
                s_prev_speed_tick_ms = out.tick_ms;
            }
            else
            {
                uint32_t dt_ms = out.tick_ms - s_prev_speed_tick_ms;

                if (dt_ms >= 100u)
                {
                    float dt_s = (float)dt_ms / 1000.0f;
                    if (dt_s > 0.0f)
                    {
                        gnss_decel_kmh_s = (s_prev_speed_kmh - out.speed_kmh) / dt_s;

                        if (isfinite(gnss_decel_kmh_s) && gnss_decel_kmh_s >= BRAKE_MIN_GNSS_DECEL)
                        {
                            gnss_decel_ok = true;
                        }
                    }

                    s_prev_speed_kmh = out.speed_kmh;
                    s_prev_speed_tick_ms = out.tick_ms;
                }
            }
        }

        bool brake_cond =
            out.have_speed &&
            out.speed_kmh >= BRAKE_MIN_SPEED_KMH &&
            isfinite(s_long_accel_filt_g) &&
            (
                (s_long_accel_filt_g <= -BRAKE_THRESH_G) ||
                ((s_long_accel_filt_g <= -0.12f) && gnss_decel_ok)
            );

        if (brake_cond)
        {
            if (s_brake_confirm_count < BRAKE_CONFIRM_SAMPLES)
            {
                s_brake_confirm_count++;
            }
        }
        else
        {
            if (s_braking && s_long_accel_filt_g >= -BRAKE_RELEASE_G)
            {
                s_braking = false;
            }
            s_brake_confirm_count = 0;
        }

        if (s_brake_confirm_count >= BRAKE_CONFIRM_SAMPLES)
        {
            s_braking = true;
        }

        out.braking_detected = s_braking;

        /* ===== Chute ===== */
        bool fall_dynamic_event =
            (isfinite(acc_norm) &&
             ((acc_norm >= (FALL_EVENT_G_HIGH * ACC_1G_REF)) ||
              (acc_norm <= (FALL_EVENT_G_LOW * ACC_1G_REF)))) ||
            (isfinite(gyro_abs_max) && (gyro_abs_max >= FALL_EVENT_GYRO_DPS));

        if (fall_dynamic_event)
        {
            s_fall_candidate = true;
            s_fall_candidate_tick_ms = out.tick_ms;
        }

        bool still_cond =
            isfinite(acc_norm) &&
            (fabsf(acc_norm - ACC_1G_REF) <= FALL_STILL_G_DELTA) &&
            isfinite(gyro_abs_max) &&
            (gyro_abs_max <= FALL_STILL_GYRO_DPS);

        bool tilt_cond = isfinite(out.bike_tilt_deg) && (out.bike_tilt_deg >= FALL_TILT_CONFIRM_DEG);
        bool speed_low_cond = (!out.have_speed) || (out.speed_kmh <= FALL_MAX_SPEED_CONFIRM_KMH);

        if (s_fall_candidate)
        {
            uint32_t elapsed_ms = out.tick_ms - s_fall_candidate_tick_ms;

            if (tilt_cond && still_cond && speed_low_cond && elapsed_ms >= FALL_CONFIRM_MS)
            {
                s_fall_latched_until_ms = out.tick_ms + FALL_LATCH_MS;
                s_fall_candidate = false;

                ESP_LOGW(TAG,
                         "CHUTE detectee: tilt=%.1f deg acc_norm=%.2f gyro_max=%.1f",
                         out.bike_tilt_deg, acc_norm, gyro_abs_max);
            }
            else if (elapsed_ms > FALL_CANDIDATE_TIMEOUT_MS)
            {
                s_fall_candidate = false;
            }
        }

        out.fall_detected = tick_after_or_equal(s_fall_latched_until_ms, out.tick_ms + 1u);
    }

    /* ==================== Snapshot global ==================== */
    lock();
    s_bike = out;
    s_ready = true;
    unlock();
}

/* ========================= Task ========================= */

static void bike_task(void *arg)
{
    (void)arg;

    if (!s_mtx)
    {
        s_mtx = xSemaphoreCreateMutex();
    }

    gnss_link_start(GNSS_UART_NUM, GNSS_TX_PIN, GNSS_RX_PIN, GNSS_BAUD);

    while (1)
    {
        fuse_once();
        vTaskDelay(pdMS_TO_TICKS(BIKE_FUSION_PERIOD_MS));
    }
}

/* ========================= API ========================= */

void bike_data_start(void)
{
    static bool started = false;

    if (started)
    {
        return;
    }
    started = true;

    if (!s_mtx)
    {
        s_mtx = xSemaphoreCreateMutex();
    }

    memset(&s_bike, 0, sizeof(s_bike));
    s_ready = false;

    xTaskCreate(bike_task, "bike_data", 4096, NULL, 6, NULL);

    ESP_LOGI(TAG, "bike_data started (UART%d RX=%d TX=%d @%d)",
             (int)GNSS_UART_NUM, GNSS_RX_PIN, GNSS_TX_PIN, GNSS_BAUD);
}

bool bike_data_get(bike_data_t *out)
{
    if (!out || !s_ready || !s_mtx)
    {
        return false;
    }

    lock();
    *out = s_bike;
    unlock();

    return true;
}