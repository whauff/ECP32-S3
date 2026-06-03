#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    float humidity_percent;
    bool valid;
} shtc3_bsp_reading_t;

esp_err_t shtc3_bsp_init(void);
esp_err_t shtc3_bsp_read(shtc3_bsp_reading_t *out_reading);

#ifdef __cplusplus
}
#endif
