#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool valid;
} rtc_bsp_datetime_t;

esp_err_t rtc_bsp_init(void);
esp_err_t rtc_bsp_get_datetime(rtc_bsp_datetime_t *out_datetime);
esp_err_t rtc_bsp_set_datetime(const rtc_bsp_datetime_t *datetime);

#ifdef __cplusplus
}
#endif
