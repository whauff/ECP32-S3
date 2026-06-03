idf_component_register(
    SRCS "main.c" "ui_home.c"
    INCLUDE_DIRS "."
    REQUIRES rlcd_bsp spi_flash lvgl_port lvgl__lvgl rtc_bsp power_bsp shtc3_bsp wifi_time_sync
)
