#include <inttypes.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "power_bsp.h"
#include "rtc_bsp.h"
#include "rlcd_bsp.h"
#include "shtc3_bsp.h"
#include "ui_home.h"
#include "wifi_time_sync.h"
#include "driver/gpio.h"

#define BOOT_BUTTON_PIN GPIO_NUM_0

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
            state.wifi_config_mode = time_sync.wifi_config_mode ? 1 : 0;
            state.ntp_synced = time_sync.time_synced ? 1 : 0;
        }

        static bool is_low_power = false;
        static bool last_btn_state = true;
        static uint32_t ms_count = 0;

        // 轮询检测 BOOT 按键状态（按下为低电平 0）
        bool current_btn_state = (gpio_get_level(BOOT_BUTTON_PIN) == 0);
        bool mode_changed = false;
        if (current_btn_state && last_btn_state) {
            // 第一次检测到按下，执行防抖
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
                is_low_power = !is_low_power;
                mode_changed = true;
                ESP_LOGI(TAG, "检测到 BOOT 按键按下！手动切换至：%s功耗模式", is_low_power ? "低" : "高");
                if (is_low_power) {
                    wifi_time_sync_power_save();
                } else {
                    wifi_time_sync_start_wifi();
                }
            }
        }
        // 只有当按键释放（高电平）后，才允许下一次触发
        last_btn_state = !current_btn_state;

        state.low_power_mode = is_low_power ? 1 : 0;

        static uint32_t refresh_count = 0;

        // 决定是否刷新显示：
        // 低功耗模式下只有在 60 秒到期，或者发生模式切换瞬间才刷新；
        // 高功耗模式下则通过计数器保持在 1 秒（1000毫秒）刷新一次。
        bool should_refresh = false;
        if (is_low_power) {
            ms_count += 100;
            if (ms_count >= 60000 || mode_changed) {
                ms_count = 0;
                should_refresh = true;
            }
        } else {
            refresh_count += 100;
            if (refresh_count >= 1000 || mode_changed) {
                refresh_count = 0;
                should_refresh = true;
            }
        }

        if (should_refresh) {
            if (lvgl_port_lock(100)) {
                ui_home_update(&state);
                lvgl_port_unlock();
            }
        }

        // 统一在主循环底部使用 100ms 快速轮询
        // 这样无论处于高功耗还是低功耗模式，短按按键都能在 100ms 内灵敏捕获！
        vTaskDelay(pdMS_TO_TICKS(100));
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

    // 初始化 BOOT 自定义按键 (GPIO0)
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_conf);

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
