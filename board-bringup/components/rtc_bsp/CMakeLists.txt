idf_component_register(
    SRCS "rtc_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2c
)
