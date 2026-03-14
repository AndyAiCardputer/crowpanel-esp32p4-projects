/*
 * NES OSD for CrowPanel Advanced 10.1" ESP32-P4
 * Platform abstraction for nofrendo NES emulator
 *
 * Display: 1024x600 landscape (EK79007, MIPI DSI) -- no rotation needed!
 * NES: 256x240 -> scaled to 800x600 (4:3, centered)
 * Input: USB HID gamepad (PS5 DualSense via Steam Deck Dock)
 * Audio: NS4168 via I2S (GPIO21=LRCLK, GPIO22=BCLK, GPIO23=SDATA, GPIO30=AMP_CTRL)
 */

#include "nes_osd.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "bsp_illuminate.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "osd.h"
#include "noftypes.h"
#include "vid_drv.h"
#include "bitmap.h"
#include "event.h"
#include "nofrendo.h"
#include "log.h"
#include "nofconfig.h"
#include "nes/nesinput.h"

#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *TAG = "NES_OSD";

// CrowPanel display: 1024x600
#define DISPLAY_WIDTH   1024
#define DISPLAY_HEIGHT  600

// NES native resolution
#define NES_SCREEN_WIDTH  256
#define NES_SCREEN_HEIGHT 240

// Scaled NES: 800x600 centered on 1024x600 (perfect 4:3)
// Y scale: 600/240 = 2.5 = 5/2
// X scale: 800/256 = 3.125 = 25/8
#define NES_DISPLAY_WIDTH   800
#define NES_DISPLAY_HEIGHT  600
#define NES_OFFSET_X        ((DISPLAY_WIDTH - NES_DISPLAY_WIDTH) / 2)  // 112

#define SCALE_X_NUM 25
#define SCALE_X_DEN 8
#define SCALE_Y_NUM 5
#define SCALE_Y_DEN 2

static uint16_t *framebuffer = NULL;
static bool display_initialized = false;

// panel_handle is a global in bsp_illuminate.c -- we reuse it directly
extern esp_lcd_panel_handle_t panel_handle;

esp_err_t nes_display_init(void)
{
    if (display_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing CrowPanel display (1024x600)...");

    // Power rails for display
    esp_ldo_channel_handle_t ldo3 = NULL, ldo4 = NULL;
    esp_ldo_channel_config_t ldo3_cfg = { .chan_id = 3, .voltage_mv = 2500 };
    esp_ldo_channel_config_t ldo4_cfg = { .chan_id = 4, .voltage_mv = 3300 };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo3_cfg, &ldo3));
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo4_cfg, &ldo4));

    // Initialize display via BSP (creates LVGL port + panel)
    esp_err_t ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "display_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    set_lcd_blight(100);

    if (!panel_handle) {
        ESP_LOGE(TAG, "lcd_panel handle is NULL");
        return ESP_FAIL;
    }

    // Allocate framebuffer in PSRAM
    size_t fb_size = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    framebuffer = (uint16_t *)heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM);
    if (!framebuffer) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer (%zu bytes)", fb_size);
        return ESP_ERR_NO_MEM;
    }
    memset(framebuffer, 0, fb_size);

    ESP_LOGI(TAG, "Framebuffer allocated in PSRAM (%zu bytes)", fb_size);

    display_initialized = true;
    ESP_LOGI(TAG, "Display ready: %dx%d, NES area: %dx%d at offset %d",
             DISPLAY_WIDTH, DISPLAY_HEIGHT, NES_DISPLAY_WIDTH, NES_DISPLAY_HEIGHT, NES_OFFSET_X);
    return ESP_OK;
}

// ============================================================================
// SIMPLE 8x8 BITMAP FONT
// ============================================================================

static const uint8_t font_8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // Space
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // !
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // "
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // #
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // $
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // %
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // &
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // '
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // (
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // )
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // *
    {0x00,0x0C,0x0C,0x7F,0x0C,0x0C,0x00,0x00}, // +
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x06,0x00}, // ,
    {0x00,0x00,0x00,0x7F,0x00,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // .
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // /
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x63,0x3E}, // 0
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x3F}, // 1
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x33,0x3F}, // 2
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x33,0x1E}, // 3
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x30,0x78}, // 4
    {0x3F,0x03,0x03,0x1F,0x30,0x30,0x33,0x1E}, // 5
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x33,0x1E}, // 6
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x0C}, // 7
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x33,0x1E}, // 8
    {0x1E,0x33,0x33,0x33,0x3E,0x30,0x18,0x0E}, // 9
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // :
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x06,0x00}, // ;
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // <
    {0x00,0x00,0x7F,0x00,0x00,0x7F,0x00,0x00}, // =
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // >
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // ?
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // @
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // A
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // B
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // C
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // D
    {0x7F,0x06,0x06,0x3E,0x06,0x06,0x7F,0x00}, // E
    {0x7F,0x06,0x06,0x3E,0x06,0x06,0x06,0x00}, // F
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // G
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // H
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // I
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // J
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // K
    {0x06,0x06,0x06,0x06,0x06,0x06,0x7F,0x00}, // L
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, // M
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // N
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // O
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x06,0x00}, // P
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // Q
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // R
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // S
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // T
    {0x33,0x33,0x33,0x33,0x33,0x33,0x1E,0x00}, // U
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // V
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // W
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // X
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // Y
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // Z
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // [
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // backslash
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // ]
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // _
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // `
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // a
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // b
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // c
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00}, // d
    {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00}, // e
    {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00}, // f
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // g
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // h
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // i
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // j
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // k
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // l
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // m
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // n
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // o
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // p
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // q
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // r
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // s
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // t
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // u
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // v
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // w
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // x
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // y
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // z
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // {
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // |
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // }
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // ~
};

static const uint8_t* get_font_char(char c) {
    if (c < 32 || c > 126) return font_8x8[0];
    return font_8x8[c - 32];
}

// Direct pixel write -- no rotation needed on CrowPanel!
static inline void set_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT) {
        framebuffer[y * DISPLAY_WIDTH + x] = color;
    }
}

void nes_display_clear(uint16_t color) {
    if (!framebuffer) return;
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        framebuffer[i] = color;
    }
}

void nes_display_draw_char(int x, int y, char c, uint16_t color, int scale) {
    if (!framebuffer) return;
    const uint8_t *data = get_font_char(c);
    for (int py = 0; py < 8; py++) {
        uint8_t row = data[py];
        for (int px = 0; px < 8; px++) {
            if (row & (1 << px)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        set_pixel(x + px * scale + sx, y + py * scale + sy, color);
                    }
                }
            }
        }
    }
}

void nes_display_draw_string(int x, int y, const char *str, uint16_t color, int scale) {
    if (!str) return;
    int cx = x;
    for (const char *p = str; *p; p++) {
        nes_display_draw_char(cx, y, *p, color, scale);
        cx += 8 * scale + 1;
    }
}

void nes_display_flush(void) {
    if (!framebuffer || !panel_handle || !display_initialized) return;
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, framebuffer);
}

// ============================================================================
// USB GAMEPAD
// ============================================================================

typedef struct {
    uint16_t buttons;
    uint8_t  dpad;
    int8_t   left_x, left_y;
    int8_t   right_x, right_y;
    uint8_t  left_trigger, right_trigger;
} usb_gamepad_state_t;

static usb_gamepad_state_t s_gamepad = {};
static bool s_gamepad_connected = false;
static uint16_t s_gp_vid = 0, s_gp_pid = 0;

static void parse_gamepad_report(const uint8_t *data, size_t len, usb_gamepad_state_t *st)
{
    if (len < 4) return;

    // Sony DualSense
    if (s_gp_vid == 0x054C && s_gp_pid == 0x0CE6) {
        if (len < 11 || data[0] != 0x01) return;
        int b = 1;
        st->left_x  = (int8_t)((int)data[b+0] - 128);
        st->left_y  = (int8_t)((int)data[b+1] - 128);
        st->right_x = (int8_t)((int)data[b+2] - 128);
        st->right_y = (int8_t)((int)data[b+3] - 128);
        st->left_trigger  = data[b+4];
        st->right_trigger = data[b+5];

        uint8_t b7 = data[b+7];
        uint8_t hat = b7 & 0x0F;
        st->dpad = (hat <= 7) ? hat + 1 : 0;

        st->buttons = 0;
        if (b7 & 0x10) st->buttons |= (1<<0);  // Square
        if (b7 & 0x20) st->buttons |= (1<<1);  // Cross
        if (b7 & 0x40) st->buttons |= (1<<2);  // Circle
        if (b7 & 0x80) st->buttons |= (1<<3);  // Triangle

        uint8_t b8 = data[b+8];
        if (b8 & 0x01) st->buttons |= (1<<4);   // L1
        if (b8 & 0x02) st->buttons |= (1<<5);   // R1
        if (b8 & 0x10) st->buttons |= (1<<6);   // Create (Select)
        if (b8 & 0x20) st->buttons |= (1<<7);   // Options (Start)
    } else {
        // Generic gamepad
        st->buttons = data[0] | (data[1] << 8);
        if (len >= 9) {
            st->dpad = data[2];
            st->left_x  = (int8_t)data[3];
            st->left_y  = (int8_t)data[4];
            st->right_x = (int8_t)data[5];
            st->right_y = (int8_t)data[6];
            st->left_trigger  = data[7];
            st->right_trigger = data[8];
        }
    }
}

void nes_usb_hid_interface_callback(hid_host_device_handle_t hid_device_handle,
                                    const hid_host_interface_event_t event, void *arg)
{
    if (event == HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
        uint8_t data[64] = {};
        size_t len = 0;
        if (hid_host_device_get_raw_input_report_data(hid_device_handle, data, sizeof(data), &len) == ESP_OK && len > 0) {
            parse_gamepad_report(data, len, &s_gamepad);
        }
    } else if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        ESP_LOGI(TAG, "USB Gamepad DISCONNECTED");
        hid_host_device_close(hid_device_handle);
        s_gamepad_connected = false;
        memset(&s_gamepad, 0, sizeof(s_gamepad));
        s_gp_vid = s_gp_pid = 0;
    }
}

void nes_usb_gamepad_set_vid_pid(uint16_t vid, uint16_t pid)
{
    s_gp_vid = vid;
    s_gp_pid = pid;
    s_gamepad_connected = true;
    ESP_LOGI(TAG, "Gamepad connected: VID=0x%04X PID=0x%04X", vid, pid);
}

// ============================================================================
// INPUT STATE
// ============================================================================

static uint32_t nes_input_state = 0xFFFFFFFF;

static void update_input_from_gamepad(uint32_t *state)
{
    if (!s_gamepad_connected) return;

    uint8_t dpad = s_gamepad.dpad;
    if (dpad==1||dpad==8||dpad==2) *state &= ~(1UL<<0); // Up
    if (dpad==5||dpad==6||dpad==4) *state &= ~(1UL<<1); // Down
    if (dpad==7||dpad==8||dpad==6) *state &= ~(1UL<<2); // Left
    if (dpad==3||dpad==2||dpad==4) *state &= ~(1UL<<3); // Right

    const int8_t th = 40;
    if (s_gamepad.left_y < -th) *state &= ~(1UL<<0);
    if (s_gamepad.left_y >  th) *state &= ~(1UL<<1);
    if (s_gamepad.left_x < -th) *state &= ~(1UL<<2);
    if (s_gamepad.left_x >  th) *state &= ~(1UL<<3);

    // Cross -> B, Circle -> A, Create -> Select, Options -> Start
    if (s_gamepad.buttons & (1<<1)) *state &= ~(1UL<<7);
    if (s_gamepad.buttons & (1<<2)) *state &= ~(1UL<<6);
    if (s_gamepad.buttons & (1<<6)) *state &= ~(1UL<<4);
    if (s_gamepad.buttons & (1<<7)) *state &= ~(1UL<<5);

    // Turbo: Square -> Turbo B, Triangle -> Turbo A
    static uint32_t turbo_cnt = 0;
    turbo_cnt++;
    uint32_t period = 6; // ~10 Hz at 60 FPS
    if (s_gamepad.buttons & (1<<0)) { // Square
        if ((turbo_cnt % period) < (period/2)) *state &= ~(1UL<<7);
    }
    if (s_gamepad.buttons & (1<<3)) { // Triangle
        if ((turbo_cnt % period) < (period/2)) *state &= ~(1UL<<6);
    }
}

void osd_getinput(void)
{
    const int ev[8] = {
        event_joypad1_up, event_joypad1_down,
        event_joypad1_left, event_joypad1_right,
        event_joypad1_select, event_joypad1_start,
        event_joypad1_a, event_joypad1_b
    };

    static uint32_t old_state = 0xFFFFFFFF;
    uint32_t state = 0xFFFFFFFF;

    update_input_from_gamepad(&state);
    nes_input_state = state;

    uint32_t changed = state ^ old_state;
    for (int i = 0; i < 8; i++) {
        if (changed & (1UL << i)) {
            event_t evh = event_get(ev[i]);
            if (evh) evh((state & (1UL<<i)) == 0 ? INP_STATE_MAKE : INP_STATE_BREAK);
        }
    }
    old_state = state;
}

void nes_input_update_state_safe(void) {
    uint32_t state = 0xFFFFFFFF;
    update_input_from_gamepad(&state);
    nes_input_state = state;
}

bool nes_input_is_up_pressed(void)    { return (nes_input_state & (1UL<<0)) == 0; }
bool nes_input_is_down_pressed(void)  { return (nes_input_state & (1UL<<1)) == 0; }
bool nes_input_is_left_pressed(void)  { return (nes_input_state & (1UL<<2)) == 0; }
bool nes_input_is_right_pressed(void) { return (nes_input_state & (1UL<<3)) == 0; }
bool nes_input_is_select_pressed(void){ return (nes_input_state & (1UL<<4)) == 0; }
bool nes_input_is_start_pressed(void) { return (nes_input_state & (1UL<<5)) == 0; }
bool nes_input_is_a_pressed(void)     { return (nes_input_state & (1UL<<6)) == 0; }
bool nes_input_is_b_pressed(void)     { return (nes_input_state & (1UL<<7)) == 0; }

esp_err_t nes_input_init(void) {
    ESP_LOGI(TAG, "Input: USB gamepad only (no I2C peripherals on CrowPanel)");
    return ESP_OK;
}

void nes_input_process(void) { osd_getinput(); }

// ============================================================================
// NOFRENDO OSD FUNCTIONS
// ============================================================================

void *mem_alloc(int size, bool prefer_fast_memory)
{
    (void)prefer_fast_memory;
    if (size > 1024) {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (p) return p;
    }
    return malloc(size);
}

static int drv_init(int w, int h) { (void)w; (void)h; return 0; }
static void drv_shutdown(void) {}
static int drv_set_mode(int w, int h) { (void)w; (void)h; return 0; }

static uint16_t nes_palette[256] = {};

static void vid_set_palette(rgb_t *pal) {
    for (int i = 0; i < 256; i++) {
        uint16_t r = (pal[i].r >> 3) & 0x1F;
        uint16_t g = (pal[i].g >> 2) & 0x3F;
        uint16_t b = (pal[i].b >> 3) & 0x1F;
        nes_palette[i] = (r << 11) | (g << 5) | b;
    }
}

static void vid_clear(uint8_t c) {
    (void)c;
    if (framebuffer) memset(framebuffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
}

static uint8_t nes_fb[NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT];
static bitmap_t *nes_bitmap = NULL;

static bitmap_t *vid_lock_write(void) {
    if (!nes_bitmap) nes_bitmap = bmp_createhw(nes_fb, NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT, NES_SCREEN_WIDTH);
    return nes_bitmap;
}

static void vid_free_write(int n, rect_t *r) { (void)n; (void)r; }

static void vid_custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties; (void)dirty_rects;
    if (!bmp || !framebuffer || !display_initialized) return;

    const uint8_t **src = (const uint8_t **)bmp->line;

    // Black sidebars
    memset(framebuffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));

    // Render NES 256x240 -> 800x600, offset by NES_OFFSET_X (112 pixels)
    for (int sy = 0; sy < NES_SCREEN_HEIGHT; sy++) {
        int base_dy = (sy * SCALE_Y_NUM) / SCALE_Y_DEN;
        int next_dy = ((sy + 1) * SCALE_Y_NUM) / SCALE_Y_DEN;
        if (sy == NES_SCREEN_HEIGHT - 1) next_dy = NES_DISPLAY_HEIGHT;

        for (int sx = 0; sx < NES_SCREEN_WIDTH; sx++) {
            uint16_t rgb565 = nes_palette[src[sy][sx]];

            int base_dx = (sx * SCALE_X_NUM) / SCALE_X_DEN;
            int next_dx = ((sx + 1) * SCALE_X_NUM) / SCALE_X_DEN;
            if (sx == NES_SCREEN_WIDTH - 1) next_dx = NES_DISPLAY_WIDTH;

            for (int dy = base_dy; dy < next_dy; dy++) {
                if (dy >= DISPLAY_HEIGHT) break;
                uint16_t *row = &framebuffer[dy * DISPLAY_WIDTH + NES_OFFSET_X + base_dx];
                int w = next_dx - base_dx;
                for (int dx = 0; dx < w; dx++) {
                    row[dx] = rgb565;
                }
            }
        }
    }

    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, framebuffer);
}

static viddriver_t crowpanel_driver = {
    "CrowPanel 10.1",
    drv_init, drv_shutdown, drv_set_mode, vid_set_palette, vid_clear,
    vid_lock_write, vid_free_write, vid_custom_blit, false
};

void osd_getvideoinfo(vidinfo_t *info) {
    info->default_width = NES_SCREEN_WIDTH;
    info->default_height = NES_SCREEN_HEIGHT;
    info->driver = &crowpanel_driver;
}

// Timer
static TimerHandle_t nes_timer = NULL;

int osd_installtimer(int freq, void *func, int funcsize, void *counter, int countersize) {
    (void)funcsize; (void)counter; (void)countersize;
    if (nes_timer) { xTimerDelete(nes_timer, 0); nes_timer = NULL; }
    nes_timer = xTimerCreate("nes", pdMS_TO_TICKS(1000/freq), pdTRUE, NULL, (TimerCallbackFunction_t)func);
    if (nes_timer) { xTimerStart(nes_timer, 0); return 0; }
    return -1;
}

// ============================================================================
// AUDIO -- NS4168 via I2S
// ============================================================================

#define AUDIO_SAMPLE_RATE   22050
#define AUDIO_BPS           16
#define AUDIO_GPIO_LRCLK    21
#define AUDIO_GPIO_BCLK     22
#define AUDIO_GPIO_SDATA    23
#define AUDIO_GPIO_AMP_CTRL 30

static i2s_chan_handle_t s_i2s_tx = NULL;
static void (*s_audio_callback)(void *buffer, int size) = NULL;
static bool s_audio_running = false;
static TaskHandle_t s_audio_task_handle = NULL;

#define AUDIO_BUF_SAMPLES 512

static void audio_playback_task(void *arg)
{
    (void)arg;
    int16_t *mono_buf = heap_caps_malloc(AUDIO_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *stereo_buf = heap_caps_malloc(AUDIO_BUF_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!mono_buf || !stereo_buf) {
        ESP_LOGE(TAG, "Audio buffer alloc failed");
        if (mono_buf) free(mono_buf);
        if (stereo_buf) free(stereo_buf);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Audio playback task started (%d Hz, %d-bit)", AUDIO_SAMPLE_RATE, AUDIO_BPS);

    while (s_audio_running) {
        if (s_audio_callback) {
            s_audio_callback(mono_buf, AUDIO_BUF_SAMPLES * sizeof(int16_t));

            for (int i = 0; i < AUDIO_BUF_SAMPLES; i++) {
                stereo_buf[i * 2]     = mono_buf[i];
                stereo_buf[i * 2 + 1] = mono_buf[i];
            }

            size_t written = 0;
            i2s_channel_write(s_i2s_tx, stereo_buf, AUDIO_BUF_SAMPLES * 2 * sizeof(int16_t),
                              &written, pdMS_TO_TICKS(100));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    free(mono_buf);
    free(stereo_buf);
    ESP_LOGI(TAG, "Audio playback task stopped");
    vTaskDelete(NULL);
}

int osd_init_sound(void)
{
    ESP_LOGI(TAG, "Initializing I2S audio (NS4168)...");

    // Amplifier control GPIO (LOW = on, HIGH = off)
    gpio_config_t amp_cfg = {
        .pin_bit_mask = 1ULL << AUDIO_GPIO_AMP_CTRL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&amp_cfg);
    gpio_set_level(AUDIO_GPIO_AMP_CTRL, 1); // off until ready

    // I2S channel
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_1,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 256,
        .auto_clear = true,
        .intr_priority = 0,
    };
    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return -1;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_GPIO_BCLK,
            .ws = AUDIO_GPIO_LRCLK,
            .dout = AUDIO_GPIO_SDATA,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ret = i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = i2s_channel_enable(s_i2s_tx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Enable amplifier
    gpio_set_level(AUDIO_GPIO_AMP_CTRL, 0);

    ESP_LOGI(TAG, "I2S audio initialized: %d Hz, 16-bit stereo", AUDIO_SAMPLE_RATE);
    return 0;
}

void osd_stopsound(void)
{
    s_audio_running = false;
    if (s_audio_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(50));
        s_audio_task_handle = NULL;
    }
    // Mute amplifier
    gpio_set_level(AUDIO_GPIO_AMP_CTRL, 1);

    if (s_i2s_tx) {
        i2s_channel_disable(s_i2s_tx);
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
    }
    s_audio_callback = NULL;
    ESP_LOGI(TAG, "Audio stopped");
}

void osd_setsound(void (*playfunc)(void *buffer, int size))
{
    s_audio_callback = playfunc;

    if (playfunc && !s_audio_running) {
        if (!s_i2s_tx) {
            ESP_LOGI(TAG, "I2S not yet initialized, calling osd_init_sound()...");
            if (osd_init_sound() != 0) {
                ESP_LOGE(TAG, "I2S init failed, no audio");
                return;
            }
        }
        s_audio_running = true;
        xTaskCreatePinnedToCore(audio_playback_task, "nes_audio", 4096, NULL, 6, &s_audio_task_handle, 1);
        ESP_LOGI(TAG, "Audio playback started on CPU1");
    }
}

void osd_getsoundinfo(sndinfo_t *info) {
    info->sample_rate = AUDIO_SAMPLE_RATE;
    info->bps = AUDIO_BPS;
}

void osd_getmouse(int *x, int *y, int *button) { (void)x; (void)y; (void)button; }

void osd_fullname(char *fullname, const char *shortname) {
    strncpy(fullname, shortname, PATH_MAX);
    fullname[PATH_MAX - 1] = '\0';
}

char *osd_newextension(char *string, char *ext) {
    size_t l = strlen(string);
    if (l >= 3 && ext && strlen(ext) >= 3) {
        string[l-3] = ext[1]; string[l-2] = ext[2]; string[l-1] = ext[3];
    }
    return string;
}

int osd_makesnapname(char *f, int l) { (void)f; (void)l; return -1; }
void osd_set_sram_ptr(uint8_t *p, int l) { (void)p; (void)l; }
const uint8_t* _get_rom_ptr(void) { return NULL; }
size_t _get_rom_size(void) { return 0; }

// ============================================================================
// OSD INIT / SHUTDOWN
// ============================================================================

esp_err_t nes_osd_init(void)
{
    ESP_LOGI(TAG, "Initializing NES OSD for CrowPanel...");
    esp_err_t ret = nes_display_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Display init failed"); return ret; }
    nes_input_init();
    ESP_LOGI(TAG, "NES OSD ready");
    return ESP_OK;
}

void nes_osd_shutdown(void) {
    display_initialized = false;
    if (framebuffer) { free(framebuffer); framebuffer = NULL; }
}

int osd_init(void) { return (nes_osd_init() == ESP_OK) ? 0 : -1; }
void osd_shutdown(void) {
    nes_osd_shutdown();
    if (nes_timer) { xTimerDelete(nes_timer, 0); nes_timer = NULL; }
    if (nes_bitmap) { bmp_destroy(&nes_bitmap); }
}

char configfilename[] = "na";

int osd_main(int argc, char *argv[]) {
    (void)argc;
    config.filename = configfilename;
    return main_loop(argv[0], system_autodetect);
}
