idf_component_register(SRCS "bsp_sd.c"
                        INCLUDE_DIRS "include"
                        REQUIRES driver fatfs sdmmc esp_driver_sdmmc)
