#include <inttypes.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "power_bsp.h"
#include "rtc_bsp.h"
#include "rlcd_bsp.h"
#include "shtc3_bsp.h"
#include "ui_home.h"
#include "wifi_time_sync.h"

static const char *TAG = "board_bringup";

static void board_ui_task(void *arg)
{
    (void)arg;

    while (1) {
        rtc_bsp_datetime_t rtc_time = {0};
        shtc3_bsp_reading_t climate = {0};
        wifi_time_sync_status_t time_sync = {0};
        ui_home_state_t state = {0};

        if (rtc_bsp_get_datetime(&rtc_time) == ESP_OK) {
            state.year = rtc_time.year;
            state.month = rtc_time.month;
            state.day = rtc_time.day;
            state.hour = rtc_time.hour;
            state.minute = rtc_time.minute;
            state.second = rtc_time.second;
            state.rtc_valid = rtc_time.valid ? 1 : 0;
        }

        state.battery_voltage = power_bsp_get_battery_voltage();
        state.battery_percent = power_bsp_get_battery_percent();
        if (shtc3_bsp_read(&climate) == ESP_OK && climate.valid) {
            state.temperature_c = climate.temperature_c;
            state.humidity_percent = climate.humidity_percent;
            state.climate_valid = 1;
        }
        if (wifi_time_sync_get_status(&time_sync) == ESP_OK) {
            state.wifi_configured = time_sync.wifi_configured ? 1 : 0;
            state.wifi_connected = time_sync.wifi_connected ? 1 : 0;
            state.ntp_synced = time_sync.time_synced ? 1 : 0;
        }

        if (lvgl_port_lock(100)) {
            ui_home_update(&state);
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_chip_info_t chip_info = {0};
    uint32_t flash_size = 0;
    esp_err_t flash_err = ESP_OK;
    esp_err_t rlcd_err = ESP_OK;
    esp_err_t lvgl_err = ESP_OK;
    esp_err_t rtc_err = ESP_OK;
    esp_err_t power_err = ESP_OK;
    esp_err_t shtc3_err = ESP_OK;
    esp_err_t wifi_time_err = ESP_OK;

    esp_chip_info(&chip_info);
    flash_err = esp_flash_get_size(NULL, &flash_size);
    if (flash_err != ESP_OK) {
        ESP_LOGW(TAG, "failed to read flash size: %s", esp_err_to_name(flash_err));
        flash_size = 0;
    }

    ESP_LOGI(TAG, "ESP32-S3-RLCD-4.2 bring-up start");
    ESP_LOGI(TAG, "cores=%d, revision=%d, flash=%" PRIu32 "MB",
             chip_info.cores,
             chip_info.revision,
             flash_size / (1024 * 1024));

    rtc_err = rtc_bsp_init();
    if (rtc_err != ESP_OK) {
        ESP_LOGW(TAG, "RTC 初始化失败: %s", esp_err_to_name(rtc_err));
    }

    power_err = power_bsp_init();
    if (power_err != ESP_OK) {
        ESP_LOGW(TAG, "电池 ADC 初始化失败: %s", esp_err_to_name(power_err));
    }

    shtc3_err = shtc3_bsp_init();
    if (shtc3_err != ESP_OK) {
        ESP_LOGW(TAG, "SHTC3 初始化失败: %s", esp_err_to_name(shtc3_err));
    }

    wifi_time_err = wifi_time_sync_init();
    if (wifi_time_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi/NTP 初始化失败: %s", esp_err_to_name(wifi_time_err));
    }

    rlcd_err = rlcd_bsp_init();
    if (rlcd_err != ESP_OK) {
        ESP_LOGE(TAG, "RLCD 初始化失败: %s", esp_err_to_name(rlcd_err));
    } else {
        lvgl_err = lvgl_port_init();
        if (lvgl_err != ESP_OK) {
            ESP_LOGE(TAG, "LVGL 初始化失败: %s", esp_err_to_name(lvgl_err));
        } else if (lvgl_port_lock(-1)) {
            ui_home_init();
            ui_home_update(&(ui_home_state_t){
                .battery_voltage = power_bsp_get_battery_voltage(),
                .battery_percent = power_bsp_get_battery_percent(),
            });
            lvgl_port_unlock();
            ESP_LOGI(TAG, "LVGL 首页已加载");

            BaseType_t ok = xTaskCreatePinnedToCore(board_ui_task, "board_ui", 4096, NULL, 4, NULL, 0);
            if (ok != pdPASS) {
                ESP_LOGE(TAG, "UI 刷新任务创建失败");
            }
        } else {
            ESP_LOGE(TAG, "LVGL 锁获取失败");
        }
    }

    while (1) {
        ESP_LOGI(TAG, "heartbeat");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
