/*
 * Test: USB Host Gamepad (PS5 DualSense) with On-Screen Log
 * CrowPanel Advanced 10.1" ESP32-P4
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bsp_illuminate.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid.h"

#define TAG "USB_GP"

/* ======================== On-Screen Log ======================== */

#define LOG_LINES 12
#define LOG_LINE_LEN 80

static char g_log_buf[LOG_LINES][LOG_LINE_LEN];
static int g_log_idx = 0;
static lv_obj_t *lbl_log = NULL;

static void screen_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_log_buf[g_log_idx % LOG_LINES], LOG_LINE_LEN, fmt, args);
    va_end(args);
    g_log_idx++;

    ESP_LOGI(TAG, "%s", g_log_buf[(g_log_idx - 1) % LOG_LINES]);
}

static void screen_log_refresh(void)
{
    if (!lbl_log) return;

    static char full_log[LOG_LINES * (LOG_LINE_LEN + 1)];
    full_log[0] = '\0';

    int total = (g_log_idx < LOG_LINES) ? g_log_idx : LOG_LINES;
    int start = (g_log_idx < LOG_LINES) ? 0 : (g_log_idx % LOG_LINES);

    for (int i = 0; i < total; i++) {
        int idx = (start + i) % LOG_LINES;
        strcat(full_log, g_log_buf[idx]);
        if (i < total - 1) strcat(full_log, "\n");
    }

    lv_label_set_text(lbl_log, full_log);
}

/* ======================== Gamepad State ======================== */

typedef struct {
    int8_t left_x, left_y;
    int8_t right_x, right_y;
    uint8_t l2_trigger, r2_trigger;
    uint16_t buttons;
    uint8_t dpad;
    bool connected;
    uint16_t vid, pid;
    char product_name[64];
} gamepad_state_t;

static volatile gamepad_state_t g_gamepad = {0};

#define BTN_SQUARE   (1 << 0)
#define BTN_CROSS    (1 << 1)
#define BTN_CIRCLE   (1 << 2)
#define BTN_TRIANGLE (1 << 3)
#define BTN_L1       (1 << 4)
#define BTN_R1       (1 << 5)
#define BTN_CREATE   (1 << 6)
#define BTN_OPTIONS  (1 << 7)
#define BTN_L3       (1 << 8)
#define BTN_R3       (1 << 9)
#define BTN_PS       (1 << 10)
#define BTN_L2       (1 << 11)
#define BTN_R2       (1 << 12)

static const char *dpad_names[] = {
    "CENTER", "UP", "UP-R", "RIGHT", "DN-R", "DOWN", "DN-L", "LEFT", "UP-L"
};

/* ======================== DualSense Parser ======================== */

static void parse_dualsense_report(const uint8_t *data, size_t len)
{
    if (len < 11) return;
    int base = (data[0] == 0x01) ? 1 : 0;

    gamepad_state_t state = {0};
    state.connected = true;
    state.vid = g_gamepad.vid;
    state.pid = g_gamepad.pid;
    memcpy(state.product_name, (void*)g_gamepad.product_name, sizeof(state.product_name));

    state.left_x  = (int8_t)((int)data[base + 0] - 128);
    state.left_y  = (int8_t)((int)data[base + 1] - 128);
    state.right_x = (int8_t)((int)data[base + 2] - 128);
    state.right_y = (int8_t)((int)data[base + 3] - 128);
    state.l2_trigger = data[base + 4];
    state.r2_trigger = data[base + 5];

    uint8_t b7 = data[base + 7];
    state.dpad = ((b7 & 0x0F) <= 7) ? ((b7 & 0x0F) + 1) : 0;

    state.buttons = 0;
    if (b7 & 0x10) state.buttons |= BTN_SQUARE;
    if (b7 & 0x20) state.buttons |= BTN_CROSS;
    if (b7 & 0x40) state.buttons |= BTN_CIRCLE;
    if (b7 & 0x80) state.buttons |= BTN_TRIANGLE;

    uint8_t b8 = data[base + 8];
    if (b8 & 0x01) state.buttons |= BTN_L1;
    if (b8 & 0x02) state.buttons |= BTN_R1;
    if (b8 & 0x04) state.buttons |= BTN_L2;
    if (b8 & 0x08) state.buttons |= BTN_R2;
    if (b8 & 0x10) state.buttons |= BTN_CREATE;
    if (b8 & 0x20) state.buttons |= BTN_OPTIONS;
    if (b8 & 0x40) state.buttons |= BTN_L3;
    if (b8 & 0x80) state.buttons |= BTN_R3;

    if (data[base + 9] & 0x01) state.buttons |= BTN_PS;
    if (state.l2_trigger > 30)  state.buttons |= BTN_L2;
    if (state.r2_trigger > 30)  state.buttons |= BTN_R2;

    g_gamepad = state;
}

static void parse_generic_gamepad(const uint8_t *data, size_t len)
{
    if (len < 4) return;
    gamepad_state_t state = {0};
    state.connected = true;
    state.vid = g_gamepad.vid;
    state.pid = g_gamepad.pid;
    memcpy(state.product_name, (void*)g_gamepad.product_name, sizeof(state.product_name));
    state.left_x  = (int8_t)((int)data[0] - 128);
    state.left_y  = (int8_t)((int)data[1] - 128);
    if (len >= 4) { state.right_x = (int8_t)((int)data[2] - 128); state.right_y = (int8_t)((int)data[3] - 128); }
    if (len >= 6) { state.buttons = (data[5] << 8) | data[4]; }
    g_gamepad = state;
}

/* ======================== USB Host ======================== */

static usb_host_client_handle_t s_usb_client = NULL;

static void usb_host_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    switch (msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            screen_log("[USB] New device addr=%d", msg->new_dev.address);
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            screen_log("[USB] Device disconnected");
            g_gamepad.connected = false;
            break;
        default:
            screen_log("[USB] Event %d", msg->event);
            break;
    }
}

static void usb_host_client_task(void *arg)
{
    while (1) {
        if (s_usb_client) {
            usb_host_client_handle_events(s_usb_client, portMAX_DELAY);
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static void usb_host_lib_task(void *arg)
{
    screen_log("[USB] Lib task started");
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            screen_log("[USB] Lib: no clients flag");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            screen_log("[USB] Lib: all free flag");
        }
    }
}

/* ======================== HID Host ======================== */

static void hid_input_report_cb(hid_host_device_handle_t dev,
                                const hid_host_interface_event_t event, void *arg)
{
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
            uint8_t data[64] = {0};
            size_t len = 0;
            esp_err_t ret = hid_host_device_get_raw_input_report_data(dev, data, sizeof(data), &len);
            if (ret != ESP_OK || len == 0) break;

            static int report_count = 0;
            if (report_count < 3) {
                screen_log("[HID] Report #%d len=%d [%02X %02X %02X %02X]",
                    report_count, len, data[0], data[1], data[2], data[3]);
                report_count++;
            }

            if (g_gamepad.vid == 0x054C && g_gamepad.pid == 0x0CE6) {
                parse_dualsense_report(data, len);
            } else {
                parse_generic_gamepad(data, len);
            }
            break;
        }
        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            screen_log("[HID] Interface disconnected");
            g_gamepad.connected = false;
            hid_host_device_close(dev);
            break;
        default:
            screen_log("[HID] Interface event %d", event);
            break;
    }
}

static void hid_device_event_cb(hid_host_device_handle_t dev,
                                const hid_host_driver_event_t event, void *arg)
{
    screen_log("[HID] Driver event %d", event);

    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        hid_host_dev_info_t info;
        esp_err_t ret = hid_host_get_device_info(dev, &info);
        if (ret == ESP_OK) {
            screen_log("[HID] VID=0x%04X PID=0x%04X", info.VID, info.PID);

            gamepad_state_t tmp = {0};
            tmp.vid = info.VID;
            tmp.pid = info.PID;
            tmp.connected = true;
            snprintf(tmp.product_name, sizeof(tmp.product_name), "VID:%04X PID:%04X", info.VID, info.PID);
            g_gamepad = tmp;
        } else {
            screen_log("[HID] get_device_info err=%s", esp_err_to_name(ret));
        }

        const hid_host_device_config_t dev_cfg = {
            .callback = hid_input_report_cb,
            .callback_arg = NULL
        };
        ret = hid_host_device_open(dev, &dev_cfg);
        screen_log("[HID] device_open: %s", esp_err_to_name(ret));
        if (ret == ESP_OK) {
            ret = hid_host_device_start(dev);
            screen_log("[HID] device_start: %s", esp_err_to_name(ret));
        }
    }
}

static void usb_host_init(void)
{
    esp_err_t ret;

    screen_log("[INIT] usb_host_install...");
    const usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ret = usb_host_install(&host_cfg);
    screen_log("[INIT] usb_host_install: %s", esp_err_to_name(ret));
    if (ret != ESP_OK) return;

    xTaskCreate(usb_host_lib_task, "usb_lib", 4096, NULL, 5, NULL);

    usb_host_client_config_t client_cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usb_host_event_cb,
            .callback_arg = NULL
        }
    };
    ret = usb_host_client_register(&client_cfg, &s_usb_client);
    screen_log("[INIT] client_register: %s", esp_err_to_name(ret));
    if (ret != ESP_OK) return;

    xTaskCreate(usb_host_client_task, "usb_client", 4096, NULL, 5, NULL);

    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_device_event_cb,
        .callback_arg = NULL
    };
    ret = hid_host_install(&hid_cfg);
    screen_log("[INIT] hid_host_install: %s", esp_err_to_name(ret));

    screen_log("[INIT] USB Host READY. Connect gamepad!");
}

/* ======================== Display ======================== */

static esp_ldo_channel_handle_t ldo3 = NULL, ldo4 = NULL;
static lv_obj_t *lbl_title = NULL;
static lv_obj_t *lbl_status = NULL;
static lv_obj_t *lbl_sticks = NULL;
static lv_obj_t *lbl_buttons = NULL;

static void system_init(void)
{
    esp_err_t err;
    esp_ldo_channel_config_t ldo3_cfg = { .chan_id = 3, .voltage_mv = 2500 };
    err = esp_ldo_acquire_channel(&ldo3_cfg, &ldo3);
    if (err != ESP_OK) { while(1) vTaskDelay(1000); }

    esp_ldo_channel_config_t ldo4_cfg = { .chan_id = 4, .voltage_mv = 3300 };
    err = esp_ldo_acquire_channel(&ldo4_cfg, &ldo4);
    if (err != ESP_OK) { while(1) vTaskDelay(1000); }

    err = display_init();
    if (err != ESP_OK) { while(1) vTaskDelay(1000); }

    set_lcd_blight(100);
}

static void create_ui(void)
{
    if (lvgl_port_lock(0) != true) return;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_make(0x00, 0x00, 0x00), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    static lv_style_t st_title, st_info, st_data, st_log;

    lv_style_init(&st_title);
    lv_style_set_text_font(&st_title, &lv_font_montserrat_20);
    lv_style_set_text_color(&st_title, lv_color_make(0x00, 0xFF, 0xFF));

    lv_style_init(&st_info);
    lv_style_set_text_font(&st_info, &lv_font_montserrat_14);
    lv_style_set_text_color(&st_info, lv_color_make(0xFF, 0xFF, 0x00));

    lv_style_init(&st_data);
    lv_style_set_text_font(&st_data, &lv_font_montserrat_14);
    lv_style_set_text_color(&st_data, lv_color_make(0xFF, 0xFF, 0xFF));

    lv_style_init(&st_log);
    lv_style_set_text_font(&st_log, &lv_font_montserrat_14);
    lv_style_set_text_color(&st_log, lv_color_make(0x00, 0xFF, 0x00));

    lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "USB Gamepad Test (CrowPanel 10.1\")");
    lv_obj_add_style(lbl_title, &st_title, LV_PART_MAIN);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 10, 5);

    lbl_status = lv_label_create(scr);
    lv_label_set_text(lbl_status, "Gamepad: not connected");
    lv_obj_add_style(lbl_status, &st_info, LV_PART_MAIN);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_LEFT, 10, 35);

    lbl_sticks = lv_label_create(scr);
    lv_label_set_text(lbl_sticks, "");
    lv_obj_add_style(lbl_sticks, &st_data, LV_PART_MAIN);
    lv_obj_align(lbl_sticks, LV_ALIGN_TOP_LEFT, 10, 60);

    lbl_buttons = lv_label_create(scr);
    lv_label_set_text(lbl_buttons, "");
    lv_obj_add_style(lbl_buttons, &st_data, LV_PART_MAIN);
    lv_obj_align(lbl_buttons, LV_ALIGN_TOP_LEFT, 10, 120);

    /* === LOG AREA (bottom half) === */
    lv_obj_t *log_line = lv_obj_create(scr);
    lv_obj_set_size(log_line, 1004, 2);
    lv_obj_set_style_bg_color(log_line, lv_color_make(0x00, 0x80, 0x00), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(log_line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(log_line, 0, LV_PART_MAIN);
    lv_obj_align(log_line, LV_ALIGN_TOP_LEFT, 10, 160);

    lv_obj_t *log_title = lv_label_create(scr);
    lv_label_set_text(log_title, "--- System Log ---");
    lv_obj_add_style(log_title, &st_info, LV_PART_MAIN);
    lv_obj_align(log_title, LV_ALIGN_TOP_LEFT, 10, 168);

    lbl_log = lv_label_create(scr);
    lv_label_set_text(lbl_log, "(starting...)");
    lv_obj_add_style(lbl_log, &st_log, LV_PART_MAIN);
    lv_obj_align(lbl_log, LV_ALIGN_TOP_LEFT, 10, 190);
    lv_label_set_long_mode(lbl_log, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_log, 1004);

    lvgl_port_unlock();
}

static const char *btn_name(uint16_t bit)
{
    switch (bit) {
        case BTN_CROSS:    return "X";
        case BTN_CIRCLE:   return "O";
        case BTN_SQUARE:   return "[]";
        case BTN_TRIANGLE: return "/\\";
        case BTN_L1:       return "L1";
        case BTN_R1:       return "R1";
        case BTN_L2:       return "L2";
        case BTN_R2:       return "R2";
        case BTN_CREATE:   return "Create";
        case BTN_OPTIONS:  return "Options";
        case BTN_PS:       return "PS";
        case BTN_L3:       return "L3";
        case BTN_R3:       return "R3";
        default:           return "?";
    }
}

static void log_gamepad_changes(const gamepad_state_t *prev, const gamepad_state_t *cur)
{
    if (!prev->connected && cur->connected) {
        ESP_LOGI(TAG, "GAMEPAD CONNECTED: %s", cur->product_name);
    } else if (prev->connected && !cur->connected) {
        ESP_LOGI(TAG, "GAMEPAD DISCONNECTED");
    }

    if (!cur->connected) return;

    uint16_t changed = prev->buttons ^ cur->buttons;
    for (int i = 0; i < 13; i++) {
        uint16_t bit = (1 << i);
        if (changed & bit) {
            ESP_LOGI(TAG, "BTN %s %s", btn_name(bit), (cur->buttons & bit) ? "PRESSED" : "RELEASED");
        }
    }

    if (prev->dpad != cur->dpad) {
        ESP_LOGI(TAG, "DPAD: %s", (cur->dpad < 9) ? dpad_names[cur->dpad] : "?");
    }

    #define STICK_THRESHOLD 20
    if (abs(cur->left_x - prev->left_x) > STICK_THRESHOLD || abs(cur->left_y - prev->left_y) > STICK_THRESHOLD) {
        ESP_LOGI(TAG, "L-STICK: X=%+d Y=%+d", cur->left_x, cur->left_y);
    }
    if (abs(cur->right_x - prev->right_x) > STICK_THRESHOLD || abs(cur->right_y - prev->right_y) > STICK_THRESHOLD) {
        ESP_LOGI(TAG, "R-STICK: X=%+d Y=%+d", cur->right_x, cur->right_y);
    }

    if (abs((int)cur->l2_trigger - (int)prev->l2_trigger) > 20) {
        ESP_LOGI(TAG, "L2: %d", cur->l2_trigger);
    }
    if (abs((int)cur->r2_trigger - (int)prev->r2_trigger) > 20) {
        ESP_LOGI(TAG, "R2: %d", cur->r2_trigger);
    }
}

static void update_display_task(void *arg)
{
    char buf[256];
    gamepad_state_t prev_gp = {0};

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (lvgl_port_lock(0) != true) continue;

        screen_log_refresh();

        gamepad_state_t gp = g_gamepad;

        log_gamepad_changes(&prev_gp, &gp);
        prev_gp = gp;

        if (!gp.connected) {
            lv_label_set_text(lbl_status, "Gamepad: not connected");
            lv_label_set_text(lbl_sticks, "");
            lv_label_set_text(lbl_buttons, "");
        } else {
            snprintf(buf, sizeof(buf), "Gamepad: %s", gp.product_name);
            lv_label_set_text(lbl_status, buf);

            snprintf(buf, sizeof(buf),
                "L-Stick: X=%+4d Y=%+4d  |  R-Stick: X=%+4d Y=%+4d  |  L2=%3d R2=%3d  |  D-Pad: %s",
                gp.left_x, gp.left_y, gp.right_x, gp.right_y,
                gp.l2_trigger, gp.r2_trigger,
                (gp.dpad < 9) ? dpad_names[gp.dpad] : "?");
            lv_label_set_text(lbl_sticks, buf);

            char btns[128] = "";
            if (gp.buttons & BTN_CROSS)    strcat(btns, "X ");
            if (gp.buttons & BTN_CIRCLE)   strcat(btns, "O ");
            if (gp.buttons & BTN_SQUARE)   strcat(btns, "[] ");
            if (gp.buttons & BTN_TRIANGLE) strcat(btns, "/\\ ");
            if (gp.buttons & BTN_L1)       strcat(btns, "L1 ");
            if (gp.buttons & BTN_R1)       strcat(btns, "R1 ");
            if (gp.buttons & BTN_L2)       strcat(btns, "L2 ");
            if (gp.buttons & BTN_R2)       strcat(btns, "R2 ");
            if (gp.buttons & BTN_CREATE)   strcat(btns, "Create ");
            if (gp.buttons & BTN_OPTIONS)  strcat(btns, "Options ");
            if (gp.buttons & BTN_PS)       strcat(btns, "PS ");
            if (btns[0] == '\0') strcpy(btns, "(none)");
            snprintf(buf, sizeof(buf), "Buttons: %s", btns);
            lv_label_set_text(lbl_buttons, buf);
        }

        lvgl_port_unlock();
    }
}

/* ======================== Main ======================== */

void app_main(void)
{
    system_init();
    create_ui();

    screen_log("[BOOT] CrowPanel USB Gamepad Test v2");
    screen_log("[BOOT] Display OK, starting USB Host...");

    usb_host_init();

    xTaskCreate(update_display_task, "disp_upd", 8192, NULL, 4, NULL);
}
