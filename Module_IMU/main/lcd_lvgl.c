// lcd_lvgl.c (LVGL v9.x) — rotation côté PANEL
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/spi_master.h"
#include "driver/ledc.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_ili9341.h"

#include "lvgl.h"

#include "lcd_lvgl.h"
#include "bike_data.h"

#define TAG "lcd_lvgl"

/* ===== Broches selon ton câblage ===== */
#define PIN_LCD_MOSI   13
#define PIN_LCD_MISO   -1
#define PIN_LCD_SCK    14
#define PIN_LCD_CS     10
#define PIN_LCD_DC     12
#define PIN_LCD_RST    11
#define PIN_LCD_BL     15

/* Écran en PAYSAGE (logique LVGL) */
#define DISP_HOR_RES   320
#define DISP_VER_RES   240

/* LVGL task */
#define LVGL_TASK_STACK    6144
#define LVGL_TASK_PRIO     6

/* Buffer LVGL (flush en bandes) */
#define LVGL_BUF_LINES     40

/* PWM Backlight */
#define BL_LEDC_TIMER      LEDC_TIMER_0
#define BL_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define BL_LEDC_CH         LEDC_CHANNEL_0
#define BL_LEDC_FREQ_HZ    5000
#define BL_LEDC_RES        LEDC_TIMER_13_BIT

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;

/* LVGL display handle (v9) */
static lv_display_t* s_disp = NULL;

/* Mutex LVGL */
static SemaphoreHandle_t s_lvgl_mtx = NULL;
static inline void lvgl_lock(void)   { if (s_lvgl_mtx) xSemaphoreTake(s_lvgl_mtx, portMAX_DELAY); }
static inline void lvgl_unlock(void) { if (s_lvgl_mtx) xSemaphoreGive(s_lvgl_mtx); }

/* -------- LVGL draw buffers -------- */
static lv_color_t* s_buf1 = NULL;
static lv_color_t* s_buf2 = NULL;

/* -------- UI objects -------- */
static lv_obj_t* s_scr = NULL;

static lv_obj_t* s_arc_speed = NULL;
static lv_obj_t* s_lbl_speed = NULL;
static lv_obj_t* s_lbl_unit  = NULL;
static lv_obj_t* s_lbl_brake = NULL;

static lv_obj_t* s_lbl_grade = NULL;
static lv_obj_t* s_lbl_alt   = NULL;
static lv_obj_t* s_lbl_dplus = NULL;

static lv_obj_t* s_lbl_head  = NULL;
static lv_obj_t* s_lbl_temp  = NULL;

/* UI panel right */
static lv_obj_t* s_panel_right = NULL;

/* Fonts fallback */
#if defined(LV_FONT_MONTSERRAT_48) && LV_FONT_MONTSERRAT_48
    #define FONT_BIG   (&lv_font_montserrat_48)
#elif defined(LV_FONT_MONTSERRAT_32) && LV_FONT_MONTSERRAT_32
    #define FONT_BIG   (&lv_font_montserrat_32)
#elif defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    #define FONT_BIG   (&lv_font_montserrat_28)
#elif defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    #define FONT_BIG   (&lv_font_montserrat_24)
#else
    #define FONT_BIG   (&lv_font_montserrat_14)
#endif

#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    #define FONT_MED   (&lv_font_montserrat_28)
#elif defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    #define FONT_MED   (&lv_font_montserrat_24)
#elif defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
    #define FONT_MED   (&lv_font_montserrat_20)
#elif defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
    #define FONT_MED   (&lv_font_montserrat_18)
#elif defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    #define FONT_MED   (&lv_font_montserrat_16)
#else
    #define FONT_MED   (&lv_font_montserrat_14)
#endif

#if defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
    #define FONT_SMALL (&lv_font_montserrat_20)
#elif defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
    #define FONT_SMALL (&lv_font_montserrat_18)
#elif defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    #define FONT_SMALL (&lv_font_montserrat_16)
#else
    #define FONT_SMALL (&lv_font_montserrat_14)
#endif

static const char* heading_to_dir_fr(float h)
{
    if (!isfinite(h)) return "--";
    if (h < 0.0f) h += 360.0f;
    if (h >= 360.0f) h = fmodf(h, 360.0f);

    if      (h < 22.5f  || h >= 337.5f) return "N";
    else if (h < 67.5f)                 return "NE";
    else if (h < 112.5f)                return "E";
    else if (h < 157.5f)                return "SE";
    else if (h < 202.5f)                return "S";
    else if (h < 247.5f)                return "SO";
    else if (h < 292.5f)                return "O";
    else                                return "NO";
}

/* Backlight PWM */
static void bl_pwm_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode       = BL_LEDC_MODE,
        .duty_resolution  = BL_LEDC_RES,
        .timer_num        = BL_LEDC_TIMER,
        .freq_hz          = BL_LEDC_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    ledc_channel_config_t ccfg = {
        .gpio_num   = PIN_LCD_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CH,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
}

void lcd_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    const uint32_t max_duty = (1u << BL_LEDC_RES) - 1u;
    uint32_t duty = (max_duty * (uint32_t)percent) / 100u;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CH, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CH);
}

/* LVGL tick */
static void lvgl_tick_cb(void* arg)
{
    (void)arg;
    lv_tick_inc(1);
}

/* Flush */
static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    (void)disp;
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

/* UI create */
static void ui_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    /* Bandeau haut */
    s_lbl_head = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_head, "CAP: ---");
    lv_obj_set_style_text_color(s_lbl_head, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_lbl_head, FONT_SMALL, 0);
    lv_obj_align(s_lbl_head, LV_ALIGN_TOP_LEFT, 10, 6);

    s_lbl_temp = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_temp, "TEMP: --.- C");
    lv_obj_set_style_text_color(s_lbl_temp, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_lbl_temp, FONT_SMALL, 0);
    lv_obj_align(s_lbl_temp, LV_ALIGN_TOP_RIGHT, -10, 6);

    /* Panneau droit */
    s_panel_right = lv_obj_create(s_scr);
    lv_obj_set_size(s_panel_right, 100, 210);
    lv_obj_align(s_panel_right, LV_ALIGN_RIGHT_MID, -4, 8);

    lv_obj_set_style_bg_color(s_panel_right, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_panel_right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel_right, 0, 0);

    lv_obj_set_style_pad_left(s_panel_right, 6, 0);
    lv_obj_set_style_pad_right(s_panel_right, 0, 0);

    /* Arc vitesse */
    s_arc_speed = lv_arc_create(s_scr);
    lv_obj_set_size(s_arc_speed, 200, 200);
    lv_obj_align(s_arc_speed, LV_ALIGN_LEFT_MID, 8, 8);

    lv_arc_set_bg_angles(s_arc_speed, 135, 45);
    lv_arc_set_range(s_arc_speed, 0, 60);
    lv_arc_set_value(s_arc_speed, 0);
    lv_obj_remove_style(s_arc_speed, NULL, LV_PART_KNOB);

    lv_obj_set_style_arc_width(s_arc_speed, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_speed, lv_color_make(210, 210, 210), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_speed, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc_speed, lv_color_black(), LV_PART_INDICATOR);

    /* Speed */
    s_lbl_speed = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_speed, "--.-");
    lv_obj_set_style_text_color(s_lbl_speed, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_lbl_speed, FONT_BIG, 0);
    lv_obj_align_to(s_lbl_speed, s_arc_speed, LV_ALIGN_CENTER, 0, -10);

    s_lbl_unit = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_unit, "km/h");
    lv_obj_set_style_text_color(s_lbl_unit, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_text_font(s_lbl_unit, FONT_SMALL, 0);
    lv_obj_align_to(s_lbl_unit, s_lbl_speed, LV_ALIGN_OUT_BOTTOM_MID, 0, -2);

    /* Message freinage sous le compteur */
    s_lbl_brake = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_brake, "Freinage");
    lv_obj_set_style_text_color(s_lbl_brake, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_text_font(s_lbl_brake, FONT_SMALL, 0);
    lv_obj_align_to(s_lbl_brake, s_lbl_unit, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_add_flag(s_lbl_brake, LV_OBJ_FLAG_HIDDEN);

    /* Colonne droite */
    lv_obj_t* lbl_grade_title = lv_label_create(s_panel_right);
    lv_label_set_text(lbl_grade_title, "PENTE");
    lv_obj_set_style_text_color(lbl_grade_title, lv_color_make(90,90,90), 0);
    lv_obj_set_style_text_font(lbl_grade_title, FONT_SMALL, 0);
    lv_obj_align(lbl_grade_title, LV_ALIGN_TOP_LEFT, 0, 10);

    s_lbl_grade = lv_label_create(s_panel_right);
    lv_label_set_text(s_lbl_grade, "--.- %");
    lv_obj_set_style_text_color(s_lbl_grade, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_lbl_grade, FONT_MED, 0);
    lv_obj_align_to(s_lbl_grade, lbl_grade_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t* lbl_alt_title = lv_label_create(s_panel_right);
    lv_label_set_text(lbl_alt_title, "ALT");
    lv_obj_set_style_text_color(lbl_alt_title, lv_color_make(90,90,90), 0);
    lv_obj_set_style_text_font(lbl_alt_title, FONT_SMALL, 0);
    lv_obj_align(lbl_alt_title, LV_ALIGN_TOP_LEFT, 0, 85);

    s_lbl_alt = lv_label_create(s_panel_right);
    lv_label_set_text(s_lbl_alt, "--- m");
    lv_obj_set_style_text_color(s_lbl_alt, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_lbl_alt, FONT_MED, 0);
    lv_obj_align_to(s_lbl_alt, lbl_alt_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t* lbl_dplus_title = lv_label_create(s_panel_right);
    lv_label_set_text(lbl_dplus_title, "D+");
    lv_obj_set_style_text_color(lbl_dplus_title, lv_color_make(90,90,90), 0);
    lv_obj_set_style_text_font(lbl_dplus_title, FONT_SMALL, 0);
    lv_obj_align(lbl_dplus_title, LV_ALIGN_TOP_LEFT, 0, 145);

    s_lbl_dplus = lv_label_create(s_panel_right);
    lv_label_set_text(s_lbl_dplus, "--- m");
    lv_obj_set_style_text_color(s_lbl_dplus, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_lbl_dplus, FONT_MED, 0);
    lv_obj_align_to(s_lbl_dplus, lbl_dplus_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_move_foreground(s_panel_right);
    lv_screen_load(s_scr);
}

/* Update UI depuis bike_data */
void ui_update_from_bike(const bike_data_t* b)
{
    if (!b || !s_scr) return;

    lvgl_lock();

    /* Speed */
    if (b->have_speed) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", b->speed_kmh);
        lv_label_set_text(s_lbl_speed, buf);

        int v = (int)lroundf(b->speed_kmh);
        if (v < 0) v = 0;
        if (v > 60) v = 60;
        lv_arc_set_value(s_arc_speed, v);
    } else {
        lv_label_set_text(s_lbl_speed, "--.-");
        lv_arc_set_value(s_arc_speed, 0);
    }

    /* Message freinage */
    if (b->braking_detected) {
        lv_obj_clear_flag(s_lbl_brake, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lbl_brake, LV_OBJ_FLAG_HIDDEN);
    }

    /* Grade */
    if (b->have_grade) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%+.1f %%", b->grade_pct);
        lv_label_set_text(s_lbl_grade, buf);
    } else {
        lv_label_set_text(s_lbl_grade, "--.- %");
    }

    /* Altitude + D+ */
    if (b->have_baro) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.0f m", b->altitude_m);
        lv_label_set_text(s_lbl_alt, buf);

        snprintf(buf, sizeof(buf), "%.0f m", b->denivele_plus_m);
        lv_label_set_text(s_lbl_dplus, buf);
    } else {
        lv_label_set_text(s_lbl_alt, "--- m");
        char buf[24];
        snprintf(buf, sizeof(buf), "%.0f m", b->denivele_plus_m);
        lv_label_set_text(s_lbl_dplus, buf);
    }

    /* Heading */
    if (b->have_heading) {
        char buf[32];
        const char* dir = heading_to_dir_fr(b->heading_deg);
        snprintf(buf, sizeof(buf), "CAP: %.0f° %s", b->heading_deg, dir);
        lv_label_set_text(s_lbl_head, buf);
    } else {
        lv_label_set_text(s_lbl_head, "CAP: ---");
    }

    /* Temp */
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "TEMP: %.1f C", b->temp_c);
        lv_label_set_text(s_lbl_temp, buf);
    }

    lvgl_unlock();
}

/* LVGL task */
static void lvgl_task(void* arg)
{
    (void)arg;
    while (1) {
        lvgl_lock();
        lv_timer_handler();
        lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* Init panel */
static void lcd_panel_init(void)
{
    ESP_LOGI(TAG, "Init SPI bus...");
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .sclk_io_num = PIN_LCD_SCK,
        .max_transfer_sz = DISP_HOR_RES * 2 * LVGL_BUF_LINES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Create IO SPI...");
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &s_io));

    ESP_LOGI(TAG, "Create panel ILI9341...");
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(s_io, &panel_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, true));
}

/* Init LVGL */
static void lvgl_init_all(void)
{
    s_lvgl_mtx = xSemaphoreCreateMutex();
    if (!s_lvgl_mtx) {
        ESP_LOGE(TAG, "LVGL mutex alloc failed");
        return;
    }

    lv_init();

    const size_t buf_pixels = (size_t)DISP_HOR_RES * (size_t)LVGL_BUF_LINES;

    s_buf1 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    s_buf2 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!s_buf1 || !s_buf2) {
        ESP_LOGE(TAG, "LVGL buffers alloc failed (need DMA-capable mem)");
        return;
    }

    s_disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);

    lv_display_set_buffers(s_disp,
                           (void*)s_buf1, (void*)s_buf2,
                           buf_pixels * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_timer_create_args_t targs = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));

    lvgl_lock();
    ui_create();
    lvgl_unlock();

    xTaskCreate(lvgl_task, "lvgl_task", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL);
}

/* Public start */
void lcd_lvgl_start(void)
{
    static bool started = false;
    if (started) return;
    started = true;

    bl_pwm_init();
    lcd_set_brightness(100);

    lcd_panel_init();
    lvgl_init_all();

    ESP_LOGI(TAG, "LCD+LVGL started (panel rotated, LVGL 320x240, WHITE UI).");
}