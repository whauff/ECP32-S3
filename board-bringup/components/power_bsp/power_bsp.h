#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t power_bsp_init(void);
float power_bsp_get_battery_voltage(void);
uint8_t power_bsp_get_battery_percent(void);

#ifdef __cplusplus
}
#endif
