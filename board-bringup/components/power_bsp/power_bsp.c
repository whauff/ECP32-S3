#include "power_bsp.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"

#define POWER_BSP_ADC_CHANNEL ADC_CHANNEL_3

static adc_cali_handle_t s_cali_handle;
static adc_oneshot_unit_handle_t s_adc_handle;
static bool s_initialized;

esp_err_t power_bsp_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    const adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };

    ESP_RETURN_ON_ERROR(adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle), "power_bsp", "ADC 校准初始化失败");
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &s_adc_handle), "power_bsp", "ADC 单次采样单元初始化失败");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, POWER_BSP_ADC_CHANNEL, &chan_config), "power_bsp", "ADC 通道配置失败");

    s_initialized = true;
    return ESP_OK;
}

float power_bsp_get_battery_voltage(void)
{
    int raw_value = 0;
    int millivolts = 0;

    if (!s_initialized) {
        return 0.0f;
    }
    if (adc_oneshot_read(s_adc_handle, POWER_BSP_ADC_CHANNEL, &raw_value) != ESP_OK) {
        return 0.0f;
    }
    if (adc_cali_raw_to_voltage(s_cali_handle, raw_value, &millivolts) != ESP_OK) {
        return 0.0f;
    }

    return 0.001f * (float)millivolts * 3.0f;
}

uint8_t power_bsp_get_battery_percent(void)
{
    const float voltage = power_bsp_get_battery_voltage();

    if (voltage <= 0.0f || voltage < 3.0f) {
        return 0;
    }
    if (voltage > 4.12f) {
        return 100;
    }

    return (uint8_t)(((voltage - 3.0f) / 1.12f) * 100.0f);
}
