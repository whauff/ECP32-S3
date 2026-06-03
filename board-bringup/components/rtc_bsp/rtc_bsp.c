#include "rtc_bsp.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

#define RTC_BSP_I2C_SDA_PIN 13
#define RTC_BSP_I2C_SCL_PIN 14
#define RTC_BSP_I2C_FREQ_HZ 300000
#define RTC_BSP_I2C_ADDR 0x51

#define RTC_REG_SECONDS 0x04
#define RTC_REG_MINUTES 0x05
#define RTC_REG_HOURS 0x06
#define RTC_REG_DAY 0x07
#define RTC_REG_MONTH 0x09
#define RTC_REG_YEAR 0x0A

static const char *TAG = "rtc_bsp";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_rtc_dev;
static bool s_initialized;

static uint8_t rtc_bsp_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static uint8_t rtc_bsp_from_bcd(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0F));
}

static bool rtc_bsp_is_leap_year(uint16_t year)
{
    return ((year % 4U == 0U) && (year % 100U != 0U)) || (year % 400U == 0U);
}

static bool rtc_bsp_is_valid_datetime(const rtc_bsp_datetime_t *dt)
{
    static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t max_day = 31;

    if (dt == NULL) {
        return false;
    }
    if (dt->year < 2024 || dt->year > 2099) {
        return false;
    }
    if (dt->month < 1 || dt->month > 12) {
        return false;
    }

    max_day = days_in_month[dt->month - 1];
    if (dt->month == 2 && rtc_bsp_is_leap_year(dt->year)) {
        max_day = 29;
    }

    if (dt->day < 1 || dt->day > max_day) {
        return false;
    }
    if (dt->hour > 23 || dt->minute > 59 || dt->second > 59) {
        return false;
    }
    return true;
}

static bool rtc_bsp_parse_build_datetime(rtc_bsp_datetime_t *out_datetime)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month_text[4] = {0};
    int month_index = -1;
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (out_datetime == NULL) {
        return false;
    }

    if (sscanf(__DATE__, "%3s %d %d", month_text, &day, &year) != 3) {
        return false;
    }
    if (sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }

    for (int i = 0; i < 12; ++i) {
        if (strncmp(&months[i * 3], month_text, 3) == 0) {
            month_index = i + 1;
            break;
        }
    }
    if (month_index < 1) {
        return false;
    }

    out_datetime->year = (uint16_t)year;
    out_datetime->month = (uint8_t)month_index;
    out_datetime->day = (uint8_t)day;
    out_datetime->hour = (uint8_t)hour;
    out_datetime->minute = (uint8_t)minute;
    out_datetime->second = (uint8_t)second;
    out_datetime->valid = rtc_bsp_is_valid_datetime(out_datetime);
    return out_datetime->valid;
}

static esp_err_t rtc_bsp_read_regs(uint8_t start_reg, uint8_t *buffer, size_t len)
{
    return i2c_master_transmit_receive(s_rtc_dev, &start_reg, 1, buffer, len, -1);
}

static esp_err_t rtc_bsp_write_regs(uint8_t start_reg, const uint8_t *buffer, size_t len)
{
    uint8_t payload[8] = {0};

    if (len + 1 > sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    payload[0] = start_reg;
    memcpy(&payload[1], buffer, len);
    return i2c_master_transmit(s_rtc_dev, payload, len + 1, -1);
}

esp_err_t rtc_bsp_set_datetime(const rtc_bsp_datetime_t *dt)
{
    uint8_t payload[7] = {0};

    ESP_RETURN_ON_FALSE(dt != NULL, ESP_ERR_INVALID_ARG, TAG, "datetime 为空");
    ESP_RETURN_ON_FALSE(rtc_bsp_is_valid_datetime(dt), ESP_ERR_INVALID_ARG, TAG, "datetime 非法");

    payload[0] = rtc_bsp_to_bcd(dt->second) & 0x7F;
    payload[1] = rtc_bsp_to_bcd(dt->minute) & 0x7F;
    payload[2] = rtc_bsp_to_bcd(dt->hour) & 0x3F;
    payload[3] = rtc_bsp_to_bcd(dt->day) & 0x3F;
    payload[4] = 0;
    payload[5] = rtc_bsp_to_bcd(dt->month) & 0x1F;
    payload[6] = rtc_bsp_to_bcd((uint8_t)(dt->year % 100U));

    return rtc_bsp_write_regs(RTC_REG_SECONDS, payload, sizeof(payload));
}

esp_err_t rtc_bsp_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = RTC_BSP_I2C_SDA_PIN,
        .scl_io_num = RTC_BSP_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RTC_BSP_I2C_ADDR,
        .scl_speed_hz = RTC_BSP_I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus), TAG, "I2C 总线初始化失败");
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_rtc_dev), TAG, "RTC 设备挂载失败");

    rtc_bsp_datetime_t dt = {0};
    esp_err_t err = rtc_bsp_get_datetime(&dt);
    if (err == ESP_OK && dt.valid) {
        ESP_LOGI(TAG, "RTC 已就绪，当前时间 %04u-%02u-%02u %02u:%02u:%02u",
                 dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
        rtc_bsp_datetime_t fallback = {0};
        if (rtc_bsp_parse_build_datetime(&fallback)) {
            ESP_LOGW(TAG, "RTC 时间无效，回写编译时间 %04u-%02u-%02u %02u:%02u:%02u",
                     fallback.year, fallback.month, fallback.day,
                     fallback.hour, fallback.minute, fallback.second);
            ESP_RETURN_ON_ERROR(rtc_bsp_set_datetime(&fallback), TAG, "RTC 回写编译时间失败");
        } else {
            ESP_LOGW(TAG, "RTC 时间无效，且编译时间解析失败");
        }
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t rtc_bsp_get_datetime(rtc_bsp_datetime_t *out_datetime)
{
    uint8_t regs[7] = {0};

    ESP_RETURN_ON_FALSE(out_datetime != NULL, ESP_ERR_INVALID_ARG, TAG, "输出参数为空");
    ESP_RETURN_ON_FALSE(s_rtc_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "RTC 未初始化");
    ESP_RETURN_ON_ERROR(rtc_bsp_read_regs(RTC_REG_SECONDS, regs, sizeof(regs)), TAG, "RTC 读取失败");

    out_datetime->second = rtc_bsp_from_bcd(regs[0] & 0x7F);
    out_datetime->minute = rtc_bsp_from_bcd(regs[1] & 0x7F);
    out_datetime->hour = rtc_bsp_from_bcd(regs[2] & 0x3F);
    out_datetime->day = rtc_bsp_from_bcd(regs[3] & 0x3F);
    out_datetime->month = rtc_bsp_from_bcd(regs[5] & 0x1F);
    out_datetime->year = (uint16_t)(2000U + rtc_bsp_from_bcd(regs[6]));
    out_datetime->valid = rtc_bsp_is_valid_datetime(out_datetime);

    return ESP_OK;
}
