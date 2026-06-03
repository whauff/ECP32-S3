idf_component_register(
    SRCS "wifi_time_sync.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event esp_netif esp_wifi esp_driver_gpio lwip nvs_flash rtc_bsp
)
