idf_component_register(SRCS "main.c"
                        REQUIRES nes_emulator bsp_sd bsp_illuminate usb_host_hid nvs_flash)
