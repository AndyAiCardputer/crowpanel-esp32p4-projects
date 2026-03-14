/*
 * Crown NES -- NES Emulator for CrowPanel Advanced 10.1 inch ESP32-P4
 *
 * Features:
 *   - NES emulation via nofrendo engine
 *   - ROM browser from SD card (.nes files in /sdcard/roms)
 *   - USB Host gamepad (PS5 DualSense via Steam Deck Dock)
 *   - 1024x600 display, NES scaled to 800x600 (4:3)
 *
 * Phase 1: Display + SD + Gamepad (no audio)
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nes_osd.h"
#include "bsp_sd.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid.h"

extern int nofrendo_main(int argc, char *argv[]);

static const char *TAG = "CROWN_NES";

#define DISPLAY_WIDTH  1024
#define DISPLAY_HEIGHT 600

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_YELLOW  0xFFE0
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_CYAN    0x07FF

#define MAX_ROMS 100
#define MAX_VISIBLE 14

static char rom_files[MAX_ROMS][256];
static int rom_count = 0;
static int selected = 0;
static bool show_browser = true;

// USB Host handles
static usb_host_client_handle_t s_usb_client = NULL;

static void usb_host_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        ESP_LOGI(TAG, "USB: New device addr=%d", msg->new_dev.address);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGI(TAG, "USB: Device disconnected");
    }
}

static void usb_host_client_task(void *arg)
{
    (void)arg;
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
    (void)arg;
    while (1) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

static void hid_device_event_cb(hid_host_device_handle_t dev,
                                const hid_host_driver_event_t event, void *arg)
{
    (void)arg;
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED) return;

    hid_host_dev_info_t info;
    if (hid_host_get_device_info(dev, &info) == ESP_OK) {
        ESP_LOGI(TAG, "Gamepad CONNECTED VID:0x%04X PID:0x%04X", info.VID, info.PID);
        nes_usb_gamepad_set_vid_pid(info.VID, info.PID);
    }

    const hid_host_device_config_t cfg = {
        .callback = nes_usb_hid_interface_callback,
        .callback_arg = NULL
    };
    ESP_ERROR_CHECK(hid_host_device_open(dev, &cfg));
    ESP_ERROR_CHECK(hid_host_device_start(dev));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Gamepad ready!");
}

static void init_usb_host(void)
{
    ESP_LOGI(TAG, "Starting USB Host...");

    const usb_host_config_t host_cfg = { .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    esp_err_t ret = usb_host_install(&host_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(ret));
        return;
    }

    xTaskCreate(usb_host_lib_task, "usb_lib", 4096, NULL, 5, NULL);

    usb_host_client_config_t client_cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = { .client_event_callback = usb_host_event_cb, .callback_arg = NULL }
    };
    ret = usb_host_client_register(&client_cfg, &s_usb_client);
    if (ret == ESP_OK) {
        xTaskCreate(usb_host_client_task, "usb_client", 4096, NULL, 5, NULL);
    }

    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_device_event_cb,
        .callback_arg = NULL
    };
    ret = hid_host_install(&hid_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "USB Host + HID ready. Connect gamepad!");
    } else {
        ESP_LOGW(TAG, "HID install failed: %s", esp_err_to_name(ret));
    }
}

static void scan_roms(void)
{
    ESP_LOGI(TAG, "Scanning " SD_MOUNT_POINT "/roms/ for .nes files...");
    rom_count = 0;

    DIR *dir = opendir(SD_MOUNT_POINT "/roms");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open /sdcard/roms");
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && rom_count < MAX_ROMS) {
        if (ent->d_name[0] == '.') continue;
        size_t len = strlen(ent->d_name);
        if (len >= 4 && strcasecmp(ent->d_name + len - 4, ".nes") == 0) {
            strncpy(rom_files[rom_count], ent->d_name, 255);
            rom_files[rom_count][255] = '\0';
            ESP_LOGI(TAG, "  Found: %s", ent->d_name);
            rom_count++;
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Found %d ROM files", rom_count);
}

static void draw_browser(void)
{
    nes_display_clear(COLOR_BLACK);

    nes_display_draw_string(20, 15, "Crown NES - Select Game", COLOR_CYAN, 3);

    if (rom_count > 0) {
        char counter[32];
        snprintf(counter, sizeof(counter), "%d/%d", selected + 1, rom_count);
        nes_display_draw_string(DISPLAY_WIDTH - 150, 20, counter, COLOR_WHITE, 2);
    }

    if (rom_count == 0) {
        nes_display_draw_string(40, DISPLAY_HEIGHT / 2 - 20, "No ROM files found", COLOR_RED, 3);
        nes_display_draw_string(40, DISPLAY_HEIGHT / 2 + 30, "Place .nes files in /sdcard/roms/", COLOR_WHITE, 2);
    } else {
        int vis_start = selected - MAX_VISIBLE / 2;
        if (vis_start < 0) vis_start = 0;
        if (vis_start + MAX_VISIBLE > rom_count) {
            vis_start = rom_count - MAX_VISIBLE;
            if (vis_start < 0) vis_start = 0;
        }

        int y = 70;
        for (int i = 0; i < MAX_VISIBLE && (vis_start + i) < rom_count; i++) {
            int idx = vis_start + i;
            if (idx == selected) {
                nes_display_draw_string(20, y, rom_files[idx], COLOR_YELLOW, 3);
            } else {
                nes_display_draw_string(20, y, rom_files[idx], COLOR_WHITE, 2);
            }
            y += 36;
        }
    }

    nes_display_draw_string(20, DISPLAY_HEIGHT - 25, "D-Pad: Navigate | Cross/Start: Launch", COLOR_GREEN, 1);

    nes_display_flush();
}

static void handle_browser_input(void)
{
    nes_input_update_state_safe();

    static bool up_prev = false, down_prev = false;
    static bool start_prev = false, a_prev = false;

    bool up = nes_input_is_up_pressed();
    bool down = nes_input_is_down_pressed();
    bool start = nes_input_is_start_pressed();
    bool a = nes_input_is_a_pressed();

    if (up && !up_prev && selected > 0) selected--;
    if (down && !down_prev && selected < rom_count - 1) selected++;
    if (((start && !start_prev) || (a && !a_prev)) && rom_count > 0) {
        show_browser = false;
    }

    up_prev = up; down_prev = down;
    start_prev = start; a_prev = a;
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Crown NES - CrowPanel 10.1\" ESP32-P4");
    ESP_LOGI(TAG, "Phase 1: Display + SD + Gamepad");
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Init display
    ret = nes_osd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OSD init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Show splash
    nes_display_clear(COLOR_BLACK);
    nes_display_draw_string(200, 200, "Crown NES", COLOR_CYAN, 4);
    nes_display_draw_string(200, 280, "CrowPanel 10.1\" ESP32-P4", COLOR_WHITE, 2);
    nes_display_draw_string(200, 320, "Loading...", COLOR_YELLOW, 2);
    nes_display_flush();

    // Init SD card
    ESP_LOGI(TAG, "Initializing SD card...");
    ret = sd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card failed: %s", esp_err_to_name(ret));
        nes_display_clear(COLOR_BLACK);
        nes_display_draw_string(40, DISPLAY_HEIGHT / 2 - 30, "SD Card Error!", COLOR_RED, 3);
        nes_display_draw_string(40, DISPLAY_HEIGHT / 2 + 20, "Insert SD card and restart", COLOR_WHITE, 2);
        nes_display_flush();
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "SD card OK");

    // Init USB Host
    init_usb_host();

    // Scan ROMs
    scan_roms();

    // File browser loop
    show_browser = true;
    selected = 0;

    while (show_browser) {
        draw_browser();
        handle_browser_input();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Launch selected ROM
    if (rom_count > 0 && selected >= 0 && selected < rom_count) {
        static char rom_path[512];
        snprintf(rom_path, sizeof(rom_path), SD_MOUNT_POINT "/roms/%s", rom_files[selected]);

        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Starting NES: %s", rom_path);
        ESP_LOGI(TAG, "========================================");

        char *argv_[1] = { rom_path };
        nofrendo_main(1, argv_);

        ESP_LOGE(TAG, "NES emulator exited!");
    }

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
