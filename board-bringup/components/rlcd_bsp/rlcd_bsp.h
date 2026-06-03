#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RLCD_BSP_WIDTH 400
#define RLCD_BSP_HEIGHT 300

typedef enum rlcd_bsp_color_t {
    RLCD_BSP_COLOR_BLACK = 0,
    RLCD_BSP_COLOR_WHITE = 1,
} rlcd_bsp_color_t;

esp_err_t rlcd_bsp_init(void);
void rlcd_bsp_fill(rlcd_bsp_color_t color);
void rlcd_bsp_draw_pixel(uint16_t x, uint16_t y, rlcd_bsp_color_t color);
void rlcd_bsp_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, rlcd_bsp_color_t color, bool filled);
esp_err_t rlcd_bsp_present(void);

#ifdef __cplusplus
}
#endif
