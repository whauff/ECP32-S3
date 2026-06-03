idf_component_register(
    SRCS "lvgl_port.c"
    INCLUDE_DIRS "."
    REQUIRES rlcd_bsp esp_timer lvgl__lvgl
)
