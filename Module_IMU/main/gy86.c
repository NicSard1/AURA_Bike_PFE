/**
 * gy86.c
 *
 * GY-86 task (ESP-IDF, I2C NG):
 *  - MPU6050
 *  - HMC5883L
 *  - MS5611
 *
 * Objectifs :
 *  - fournir des données capteurs robustes
 *  - éviter les reboot si un capteur I2C décroche
 *  - filtrer l'altitude barométrique
 *  - calculer un D+ stable
 *  - réinitialiser proprement l'état du baro après pertes prolongées
 *
 * Repère vélo utilisé ici :
 *  - X = avant du vélo
 *  - Y = gauche du vélo
 *  - Z = haut
 *
 * Ton montage indiqué :
 *  - symbole Y du module vers la gauche
 *  - symbole X du module vers le bas
 *
 * Donc :
 *  - X_bike = -X_board
 *  - Y_bike = +Y_board
 *  - Z_bike = +Z_board   (à ajuster uniquement si la carte est retournée)
 */

#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"

#include "i2cdev.h"
#include "mpu6050.h"
#include "ms5611.h"
#include "hmc5883l.h"
#include "gy86.h"

#define I2C_PORT 0
#define PIN_SDA 8
#define PIN_SCL 9
#define I2C_FREQ_HZ 50000

#define TASK_STACK_SZ 4096
#define TASK_PRIO 5

static const char *TAG = "gy86";

/* ========================= Altitude / D+ ========================= */

/* Filtrage */
#define ALT_FAST_ALPHA 0.05f
#define ALT_DPLUS_ALPHA 0.08f
#define DPLUS_STEP_THRESH_M 1.00f

/* Rejet d'aberrations */
#define BARO_WARMUP_SAMPLES 20
#define BARO_MAX_STEP_M 2.5f
#define BARO_MAX_PRESS_STEP_PA 40

/* Reset si pertes prolongées */
#define BARO_FAIL_RESET_COUNT 8

/* Pression / altitude */
#define BARO_MIN_PRESSURE_PA 10000
#define BARO_MAX_PRESSURE_PA 120000
#define SEA_LEVEL_PRESSURE_PA 101325.0f

/* Debug */
#define BARO_DEBUG_EVERY_N 10

/* ========================= MPU6050 bypass ========================= */

#ifndef MPU6050_REG_USER_CTRL
#define MPU6050_REG_USER_CTRL 0x6A
#endif
#ifndef MPU6050_REG_INT_PIN_CFG
#define MPU6050_REG_INT_PIN_CFG 0x37
#endif
#ifndef MPU6050_BIT_I2C_BYPASS_EN
#define MPU6050_BIT_I2C_BYPASS_EN (1 << 1)
#endif
#ifndef MPU6050_BIT_I2C_MST_EN
#define MPU6050_BIT_I2C_MST_EN (1 << 5)
#endif

/* ========================= Snapshot partagé ========================= */

static gy86_snapshot_t g_snap;
static SemaphoreHandle_t g_snap_mtx = NULL;
static bool g_ready = false;

/* ========================= Etat filtre baro ========================= */

static bool s_alt_init = false;
static float s_alt_filt = 0.0f;
static float s_alt_dplus = 0.0f;
static float s_alt_dplus_prev = 0.0f;
static float s_dplus_m = 0.0f;

static int s_baro_warmup_count = 0;
static bool s_baro_prev_valid = false;
static int32_t s_prev_pressure_pa = 0;
static float s_prev_altitude_m = 0.0f;
static int s_baro_fail_count = 0;

/* ================================================================== */

static inline void snap_lock(void)
{
    if (g_snap_mtx)
    {
        xSemaphoreTake(g_snap_mtx, portMAX_DELAY);
    }
}

static inline void snap_unlock(void)
{
    if (g_snap_mtx)
    {
        xSemaphoreGive(g_snap_mtx);
    }
}

/**
 * Conversion repère carte -> repère vélo
 *
 * Entrées :
 *  bx, by, bz = axes "board" du capteur
 *
 * Sorties :
 *  vx = avant
 *  vy = gauche
 *  vz = haut
 */
static inline void map_board_to_bike_frame(float bx, float by, float bz,
                                           float *vx, float *vy, float *vz)
{
    *vx = -bx; // avant
    *vy = by;  // gauche
    *vz = bz;  // haut
}

static void i2c_master_init_safe(void)
{
    /* Pull-up internes en dépannage.
       Les externes restent recommandées si le câblage le permet. */
    gpio_set_pull_mode((gpio_num_t)PIN_SDA, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)PIN_SCL, GPIO_PULLUP_ONLY);

    ESP_ERROR_CHECK(i2cdev_init());
}

static inline bool pressure_sane(int32_t p_pa)
{
    return (p_pa >= BARO_MIN_PRESSURE_PA) && (p_pa <= BARO_MAX_PRESSURE_PA);
}

static float altitude_from_pressure_msl(float p_pa)
{
    return 44330.0f * (1.0f - powf(p_pa / SEA_LEVEL_PRESSURE_PA, 0.1903f));
}

static void reset_baro_state(bool reset_dplus)
{
    s_alt_init = false;
    s_alt_filt = 0.0f;
    s_alt_dplus = 0.0f;
    s_alt_dplus_prev = 0.0f;

    s_baro_warmup_count = 0;
    s_baro_prev_valid = false;
    s_prev_pressure_pa = 0;
    s_prev_altitude_m = 0.0f;
    s_baro_fail_count = 0;

    if (reset_dplus)
    {
        s_dplus_m = 0.0f;
    }
}

static esp_err_t mpu6050_enable_bypass_safe(mpu6050_dev_t *dev, bool enable)
{
    uint8_t user_ctrl = 0;
    uint8_t int_pin = 0;
    esp_err_t err;

    err = i2c_dev_read_reg(&dev->i2c_dev, MPU6050_REG_USER_CTRL, &user_ctrl, 1);
    if (err != ESP_OK)
        return err;

    if (user_ctrl & MPU6050_BIT_I2C_MST_EN)
    {
        user_ctrl &= ~MPU6050_BIT_I2C_MST_EN;
        err = i2c_dev_write_reg(&dev->i2c_dev, MPU6050_REG_USER_CTRL, &user_ctrl, 1);
        if (err != ESP_OK)
            return err;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    err = i2c_dev_read_reg(&dev->i2c_dev, MPU6050_REG_INT_PIN_CFG, &int_pin, 1);
    if (err != ESP_OK)
        return err;

    if (enable)
    {
        int_pin |= MPU6050_BIT_I2C_BYPASS_EN;
    }
    else
    {
        int_pin &= ~MPU6050_BIT_I2C_BYPASS_EN;
    }

    err = i2c_dev_write_reg(&dev->i2c_dev, MPU6050_REG_INT_PIN_CFG, &int_pin, 1);
    if (err != ESP_OK)
        return err;

    vTaskDelay(pdMS_TO_TICKS(2));
    return ESP_OK;
}

bool gy86_get_snapshot(gy86_snapshot_t *out)
{
    if (!out || !g_snap_mtx || !g_ready)
    {
        return false;
    }

    snap_lock();
    *out = g_snap;
    snap_unlock();

    return true;
}

static void gy86_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(50));
    i2c_master_init_safe();

    /* ==================== MPU6050 ==================== */
    mpu6050_dev_t imu = {0};
    bool have_imu = false;

    if (mpu6050_init_desc(&imu, MPU6050_I2C_ADDRESS_LOW, I2C_PORT, PIN_SDA, PIN_SCL) == ESP_OK)
    {
        imu.i2c_dev.cfg.master.clk_speed = I2C_FREQ_HZ;

        if (mpu6050_init(&imu) == ESP_OK &&
            mpu6050_set_full_scale_accel_range(&imu, MPU6050_ACCEL_RANGE_4) == ESP_OK &&
            mpu6050_set_full_scale_gyro_range(&imu, MPU6050_GYRO_RANGE_500) == ESP_OK &&
            mpu6050_enable_bypass_safe(&imu, true) == ESP_OK)
        {
            have_imu = true;
            ESP_LOGI(TAG, "MPU6050 detecte sur 0x68");
        }
        else
        {
            ESP_LOGW(TAG, "MPU6050 init KO");
        }
    }
    else
    {
        ESP_LOGW(TAG, "MPU6050 absent / desc init KO");
    }

    /* ==================== MS5611 ==================== */
    ms5611_t baro = {0};
    bool have_ms = false;
    uint8_t ms_addr = 0x00;

    if (ms5611_init_desc(&baro, 0x77, I2C_PORT, PIN_SDA, PIN_SCL) == ESP_OK)
    {
        baro.i2c_dev.cfg.master.clk_speed = I2C_FREQ_HZ;
        vTaskDelay(pdMS_TO_TICKS(20));

        if (ms5611_init(&baro, MS5611_OSR_4096) == ESP_OK)
        {
            have_ms = true;
            ms_addr = 0x77;
            ESP_LOGI(TAG, "MS5611 detecte sur 0x%02X", ms_addr);
        }
        else
        {
            ms5611_free_desc(&baro);
        }
    }

    if (!have_ms)
    {
        if (ms5611_init_desc(&baro, 0x76, I2C_PORT, PIN_SDA, PIN_SCL) == ESP_OK)
        {
            baro.i2c_dev.cfg.master.clk_speed = I2C_FREQ_HZ;
            vTaskDelay(pdMS_TO_TICKS(20));

            if (ms5611_init(&baro, MS5611_OSR_4096) == ESP_OK)
            {
                have_ms = true;
                ms_addr = 0x76;
                ESP_LOGI(TAG, "MS5611 detecte sur 0x%02X", ms_addr);
            }
            else
            {
                ms5611_free_desc(&baro);
            }
        }
    }

    if (!have_ms)
    {
        ESP_LOGW(TAG, "MS5611 introuvable / init KO sur 0x77 et 0x76");
    }

    /* ==================== HMC5883L ==================== */
    hmc5883l_dev_t hmc = {0};
    bool have_hmc = false;

    if (have_imu && hmc5883l_init_desc(&hmc, I2C_PORT, PIN_SDA, PIN_SCL) == ESP_OK)
    {
        hmc.i2c_dev.cfg.master.clk_speed = I2C_FREQ_HZ;

        if (hmc5883l_set_samples_averaged(&hmc, HMC5883L_SAMPLES_8) == ESP_OK &&
            hmc5883l_set_data_rate(&hmc, HMC5883L_DATA_RATE_15_00) == ESP_OK &&
            hmc5883l_set_gain(&hmc, HMC5883L_GAIN_1090) == ESP_OK &&
            hmc5883l_set_opmode(&hmc, HMC5883L_MODE_CONTINUOUS) == ESP_OK)
        {
            have_hmc = true;
            ESP_LOGI(TAG, "HMC5883L configure");
        }
        else
        {
            ESP_LOGW(TAG, "HMC5883L config KO");
        }
    }
    else
    {
        ESP_LOGW(TAG, "HMC5883L absent ou bypass MPU indisponible");
    }

    ESP_LOGI(TAG, "GY-86 demarre (SDA=%d, SCL=%d, %d Hz)", PIN_SDA, PIN_SCL, I2C_FREQ_HZ);

    mpu6050_acceleration_t acc = {0};
    mpu6050_rotation_t gyro = {0};
    float imu_temp = 0.0f;

    int32_t pressure_pa = 0;
    float baro_temp_c = 0.0f;

    hmc5883l_data_t mag_mg = {0};

    const float RAD2DEG = 57.2957795f;
    uint32_t debug_counter = 0;

    reset_baro_state(true);

    while (1)
    {
        bool ok_imu = false;
        bool ok_mag = false;
        bool ok_baro = false;

        float heading_deg = 0.0f;
        float altitude_m = 0.0f;
        float altitude_filt_m = s_alt_filt;
        float dplus_m = s_dplus_m;

        /* Variables remappées dans le repère vélo */
        float acc_x_bike = 0.0f, acc_y_bike = 0.0f, acc_z_bike = 0.0f;
        float gyro_x_bike = 0.0f, gyro_y_bike = 0.0f, gyro_z_bike = 0.0f;
        float mag_x_bike = 0.0f, mag_y_bike = 0.0f, mag_z_bike = 0.0f;

        /* ==================== IMU ==================== */
        if (have_imu)
        {
            ok_imu =
                (mpu6050_get_acceleration(&imu, &acc) == ESP_OK) &&
                (mpu6050_get_rotation(&imu, &gyro) == ESP_OK) &&
                (mpu6050_get_temperature(&imu, &imu_temp) == ESP_OK);

            if (!ok_imu)
            {
                ESP_LOGW(TAG, "MPU6050 read failed");
            }
            else
            {
                map_board_to_bike_frame(acc.x, acc.y, acc.z,
                                        &acc_x_bike, &acc_y_bike, &acc_z_bike);

                map_board_to_bike_frame(gyro.x, gyro.y, gyro.z,
                                        &gyro_x_bike, &gyro_y_bike, &gyro_z_bike);
            }
        }

        /* ==================== MAG ==================== */
        if (have_hmc)
        {
            if (hmc5883l_get_data(&hmc, &mag_mg) == ESP_OK)
            {
                map_board_to_bike_frame(mag_mg.x, mag_mg.y, mag_mg.z,
                                        &mag_x_bike, &mag_y_bike, &mag_z_bike);

                heading_deg = atan2f(-mag_y_bike, -mag_x_bike) * RAD2DEG;

                if (heading_deg < 0.0f)
                {
                    heading_deg += 360.0f;
                }

                ok_mag = true;
            }
        }

        /* ==================== BARO ==================== */
        if (have_ms)
        {
            int32_t pressure_raw_pa = 0;
            float baro_temp_raw_c = 0.0f;

            if (ms5611_get_sensor_data(&baro, &pressure_raw_pa, &baro_temp_raw_c) == ESP_OK)
            {
                if (pressure_sane(pressure_raw_pa))
                {
                    float altitude_raw_m = altitude_from_pressure_msl((float)pressure_raw_pa);
                    bool accept_sample = true;

                    if (s_baro_prev_valid)
                    {
                        int32_t dp = pressure_raw_pa - s_prev_pressure_pa;
                        if (dp < 0)
                            dp = -dp;

                        float da = altitude_raw_m - s_prev_altitude_m;
                        if (da < 0.0f)
                            da = -da;

                        if (dp > BARO_MAX_PRESS_STEP_PA || da > BARO_MAX_STEP_M)
                        {
                            accept_sample = false;
                            ESP_LOGW(TAG,
                                     "BARO outlier rejete: P=%" PRId32 " Pa ALT=%.2f m (prev P=%" PRId32 ", prev ALT=%.2f)",
                                     pressure_raw_pa, altitude_raw_m,
                                     s_prev_pressure_pa, s_prev_altitude_m);
                        }
                    }

                    if (accept_sample)
                    {
                        pressure_pa = pressure_raw_pa;
                        baro_temp_c = baro_temp_raw_c;
                        altitude_m = altitude_raw_m;

                        s_prev_pressure_pa = pressure_pa;
                        s_prev_altitude_m = altitude_m;
                        s_baro_prev_valid = true;
                        s_baro_fail_count = 0;

                        if (s_baro_warmup_count < BARO_WARMUP_SAMPLES)
                        {
                            s_baro_warmup_count++;

                            if (s_baro_warmup_count == 1)
                            {
                                s_alt_init = true;
                                s_alt_filt = altitude_m;
                                s_alt_dplus = altitude_m;
                                s_alt_dplus_prev = altitude_m;
                                s_dplus_m = 0.0f;
                            }
                            else
                            {
                                s_alt_filt += 0.20f * (altitude_m - s_alt_filt);
                                s_alt_dplus = s_alt_filt;
                                s_alt_dplus_prev = s_alt_filt;
                            }

                            ok_baro = true;
                        }
                        else
                        {
                            ok_baro = true;

                            if (!s_alt_init)
                            {
                                s_alt_init = true;
                                s_alt_filt = altitude_m;
                                s_alt_dplus = altitude_m;
                                s_alt_dplus_prev = altitude_m;
                                s_dplus_m = 0.0f;
                            }
                            else
                            {
                                s_alt_filt += ALT_FAST_ALPHA * (altitude_m - s_alt_filt);
                                s_alt_dplus += ALT_DPLUS_ALPHA * (altitude_m - s_alt_dplus);

                                float delta_up = s_alt_dplus - s_alt_dplus_prev;
                                if (delta_up > DPLUS_STEP_THRESH_M)
                                {
                                    s_dplus_m += delta_up;
                                }
                                s_alt_dplus_prev = s_alt_dplus;
                            }
                        }

                        altitude_filt_m = s_alt_filt;
                        dplus_m = s_dplus_m;
                    }
                }
                else
                {
                    s_baro_fail_count++;
                }
            }
            else
            {
                s_baro_fail_count++;
            }

            if (!ok_baro && s_baro_fail_count >= BARO_FAIL_RESET_COUNT)
            {
                ESP_LOGW(TAG, "BARO perdu trop longtemps -> reset filtre et D+");
                reset_baro_state(true);
            }
        }

        /* ==================== Debug ==================== */
        debug_counter++;
        if (ok_baro && (debug_counter % BARO_DEBUG_EVERY_N == 0))
        {
            ESP_LOGI(TAG,
                     "BARO[0x%02X] P=%" PRId32 " Pa T=%.2f C ALT=%.2f m FILT=%.2f m D+=%.2f m",
                     ms_addr, pressure_pa, baro_temp_c, altitude_m, altitude_filt_m, dplus_m);
        }

        if (ok_mag && (debug_counter % BARO_DEBUG_EVERY_N == 0))
        {
            ESP_LOGI(TAG,
                     "MAG board=(%.1f, %.1f, %.1f) bike=(%.1f, %.1f, %.1f) heading=%.1f deg",
                     mag_mg.x, mag_mg.y, mag_mg.z,
                     mag_x_bike, mag_y_bike, mag_z_bike,
                     heading_deg);
        }

        /* ==================== Snapshot thread-safe ==================== */
        if (g_snap_mtx)
        {
            snap_lock();

            g_snap.have_imu = ok_imu;
            if (ok_imu)
            {
                g_snap.acc_x = acc_x_bike;
                g_snap.acc_y = acc_y_bike;
                g_snap.acc_z = acc_z_bike;

                g_snap.gyro_x = gyro_x_bike;
                g_snap.gyro_y = gyro_y_bike;
                g_snap.gyro_z = gyro_z_bike;

                g_snap.imu_temp_c = imu_temp;
            }

            g_snap.have_mag = ok_mag;
            if (ok_mag)
            {
                g_snap.mag_x_mg = mag_x_bike;
                g_snap.mag_y_mg = mag_y_bike;
                g_snap.mag_z_mg = mag_z_bike;
                g_snap.heading_deg = heading_deg;
            }

            g_snap.have_baro = ok_baro;
            if (ok_baro)
            {
                g_snap.pressure_pa = pressure_pa;
                g_snap.baro_temp_c = baro_temp_c;
                g_snap.altitude_m = altitude_m;
                g_snap.altitude_filt_m = altitude_filt_m;
                g_snap.denivele_plus_m = dplus_m;
            }
            else
            {
                g_snap.denivele_plus_m = s_dplus_m;
            }

            g_snap.tick_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            g_ready = true;

            snap_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void gy86_start(void)
{
    static bool started = false;
    if (started)
    {
        return;
    }
    started = true;

    if (!g_snap_mtx)
    {
        g_snap_mtx = xSemaphoreCreateMutex();
    }

    memset(&g_snap, 0, sizeof(g_snap));
    g_ready = false;

    xTaskCreate(gy86_task, "gy86_task", TASK_STACK_SZ, NULL, TASK_PRIO, NULL);
}