FILE(GLOB_RECURSE nofrendo_srcs "nofrendo/*.c")

idf_component_register(
    SRCS "src/nes_osd.c" ${nofrendo_srcs}
    INCLUDE_DIRS "include" "nofrendo" "nofrendo/cpu" "nofrendo/nes" "nofrendo/sndhrdw"
    REQUIRES bsp_illuminate bsp_sd usb_host_hid esp_lcd
    PRIV_REQUIRES esp_system esp_timer freertos esp_driver_i2s esp_driver_gpio
)
