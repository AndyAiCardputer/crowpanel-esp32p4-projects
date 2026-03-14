/*
 * Test: Display Color Cycle
 * CrowPanel Advanced 10.1" ESP32-P4
 *
 * Background cycles through colors: Red -> Yellow -> Green -> Cyan -> Blue -> Magenta
 * Text "Hello Andy+Ai" centered on screen with contrasting color
 */

#include "bsp_illuminate.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"

#define TAG "COLOR_TEST"

static esp_ldo_channel_handle_t ldo4 = NULL;
static esp_ldo_channel_handle_t ldo3 = NULL;

typedef struct {
    lv_color_t bg_color;
    lv_color_t text_color;
    const char *color_name;
} color_step_t;

static const color_step_t color_steps[] = {
    { .bg_color = {.full = 0}, .text_color = {.full = 0}, .color_name = "RED" },
    { .bg_color = {.full = 0}, .text_color = {.full = 0}, .color_name = "YELLOW" },
    { .bg_color = {.full = 0}, .text_color = {.full = 0}, .color_name = "GREEN" },
    { .bg_color = {.full = 0}, .text_color = {.full = 0}, .color_name = "CYAN" },
    { .bg_color = {.full = 0}, .text_color = {.full = 0}, .color_name = "BLUE" },
    { .bg_color = {.full = 0}, .text_color = {.full = 0}, .color_name = "MAGENTA" },
};
#define NUM_COLORS 6

static lv_color_t bg_colors[NUM_COLORS];
static lv_color_t text_colors[NUM_COLORS];

static void init_colors(void)
{
    bg_colors[0] = lv_color_make(0xFF, 0x00, 0x00);  // Red
    bg_colors[1] = lv_color_make(0xFF, 0xFF, 0x00);  // Yellow
    bg_colors[2] = lv_color_make(0x00, 0xCC, 0x00);  // Green
    bg_colors[3] = lv_color_make(0x00, 0xDD, 0xDD);  // Cyan
    bg_colors[4] = lv_color_make(0x00, 0x00, 0xFF);  // Blue
    bg_colors[5] = lv_color_make(0xFF, 0x00, 0xFF);  // Magenta

    text_colors[0] = lv_color_make(0xFF, 0xFF, 0xFF); // White on Red
    text_colors[1] = lv_color_make(0x00, 0x00, 0x00); // Black on Yellow
    text_colors[2] = lv_color_make(0xFF, 0xFF, 0xFF); // White on Green
    text_colors[3] = lv_color_make(0x00, 0x00, 0x00); // Black on Cyan
    text_colors[4] = lv_color_make(0xFF, 0xFF, 0xFF); // White on Blue
    text_colors[5] = lv_color_make(0xFF, 0xFF, 0xFF); // White on Magenta
}

static void init_fail_handler(const char *module_name, esp_err_t err)
{
    while (1) {
        ESP_LOGE(TAG, "[%s] init failed: %s", module_name, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void system_init(void)
{
    esp_err_t err;

    esp_ldo_channel_config_t ldo3_cfg = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    err = esp_ldo_acquire_channel(&ldo3_cfg, &ldo3);
    if (err != ESP_OK) init_fail_handler("LDO3", err);

    esp_ldo_channel_config_t ldo4_cfg = {
        .chan_id = 4,
        .voltage_mv = 3300,
    };
    err = esp_ldo_acquire_channel(&ldo4_cfg, &ldo4);
    if (err != ESP_OK) init_fail_handler("LDO4", err);

    err = display_init();
    if (err != ESP_OK) init_fail_handler("LCD", err);
    ESP_LOGI(TAG, "LCD init OK");

    err = set_lcd_blight(100);
    if (err != ESP_OK) init_fail_handler("Backlight", err);
    ESP_LOGI(TAG, "Backlight ON (100%%)");
}

static void color_cycle_task(void *arg)
{
    lv_obj_t *screen = NULL;
    lv_obj_t *label = NULL;
    lv_obj_t *color_label = NULL;
    static lv_style_t bg_style;
    static lv_style_t text_style;
    static lv_style_t info_style;
    int color_idx = 0;

    if (lvgl_port_lock(0) != true) {
        ESP_LOGE(TAG, "LVGL lock failed");
        vTaskDelete(NULL);
        return;
    }

    screen = lv_scr_act();

    lv_style_init(&bg_style);
    lv_style_set_bg_opa(&bg_style, LV_OPA_COVER);
    lv_style_set_bg_color(&bg_style, bg_colors[0]);
    lv_obj_add_style(screen, &bg_style, LV_PART_MAIN);

    label = lv_label_create(screen);
    lv_label_set_text(label, "Hello Andy+Ai");
    lv_style_init(&text_style);
    lv_style_set_text_font(&text_style, &lv_font_montserrat_42);
    lv_style_set_text_color(&text_style, text_colors[0]);
    lv_style_set_bg_opa(&text_style, LV_OPA_TRANSP);
    lv_obj_add_style(label, &text_style, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -30);

    color_label = lv_label_create(screen);
    lv_label_set_text(color_label, "RED");
    lv_style_init(&info_style);
    lv_style_set_text_font(&info_style, &lv_font_montserrat_14);
    lv_style_set_text_color(&info_style, text_colors[0]);
    lv_style_set_bg_opa(&info_style, LV_OPA_TRANSP);
    lv_obj_add_style(color_label, &info_style, LV_PART_MAIN);
    lv_obj_align(color_label, LV_ALIGN_CENTER, 0, 40);

    lvgl_port_unlock();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        color_idx = (color_idx + 1) % NUM_COLORS;

        if (lvgl_port_lock(0) == true) {
            lv_style_set_bg_color(&bg_style, bg_colors[color_idx]);
            lv_obj_report_style_change(&bg_style);

            lv_style_set_text_color(&text_style, text_colors[color_idx]);
            lv_obj_report_style_change(&text_style);

            lv_style_set_text_color(&info_style, text_colors[color_idx]);
            lv_obj_report_style_change(&info_style);

            lv_label_set_text(color_label, color_steps[color_idx].color_name);

            lvgl_port_unlock();
        }

        ESP_LOGI(TAG, "Color: %s (%d/%d)", color_steps[color_idx].color_name, color_idx + 1, NUM_COLORS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== CrowPanel Display Color Test ===");
    ESP_LOGI(TAG, "Resolution: 1024x600, 16-bit RGB565");

    init_colors();
    system_init();

    xTaskCreate(color_cycle_task, "color_cycle", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Color cycle started (2 sec interval)");
}
