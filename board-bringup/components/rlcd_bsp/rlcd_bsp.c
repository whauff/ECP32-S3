#include "rlcd_bsp.h"

#include <assert.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RLCD_DC_PIN GPIO_NUM_5
#define RLCD_CS_PIN GPIO_NUM_40
#define RLCD_SCK_PIN GPIO_NUM_11
#define RLCD_MOSI_PIN GPIO_NUM_12
#define RLCD_RST_PIN GPIO_NUM_41

static const char *TAG = "rlcd_bsp";
static esp_lcd_panel_io_handle_t s_io_handle;
static uint8_t *s_framebuffer;
static size_t s_framebuffer_len;
static bool s_initialized;

static inline uint8_t rlcd_bsp_raw_color(rlcd_bsp_color_t color)
{
    return color == RLCD_BSP_COLOR_WHITE ? 0xFF : 0x00;
}

static void rlcd_bsp_reset(void)
{
    gpio_set_level(RLCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RLCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(RLCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static esp_err_t rlcd_bsp_send_command(uint8_t command)
{
    return esp_lcd_panel_io_tx_param(s_io_handle, command, NULL, 0);
}

static esp_err_t rlcd_bsp_send_data(uint8_t data)
{
    return esp_lcd_panel_io_tx_param(s_io_handle, -1, &data, 1);
}

static esp_err_t rlcd_bsp_run_init_sequence(void)
{
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xD6), TAG, "send 0xD6 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x17), TAG, "send 0x17 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x02), TAG, "send 0x02 failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xD1), TAG, "send 0xD1 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x01), TAG, "send booster failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xC0), TAG, "send 0xC0 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x11), TAG, "send gate voltage 1 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x04), TAG, "send gate voltage 2 failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xC1), TAG, "send 0xC1 failed");
    for (int i = 0; i < 4; ++i) {
        ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x69), TAG, "send 0x69 failed");
    }

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xC2), TAG, "send 0xC2 failed");
    for (int i = 0; i < 4; ++i) {
        ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x19), TAG, "send 0x19 failed");
    }

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xC4), TAG, "send 0xC4 failed");
    for (int i = 0; i < 4; ++i) {
        ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x4B), TAG, "send 0x4B failed");
    }

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xC5), TAG, "send 0xC5 failed");
    for (int i = 0; i < 4; ++i) {
        ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x19), TAG, "send 0x19 repeat failed");
    }

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xD8), TAG, "send 0xD8 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x80), TAG, "send 0x80 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0xE9), TAG, "send 0xE9 failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xB2), TAG, "send 0xB2 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x02), TAG, "send 0x02 failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xB3), TAG, "send 0xB3 failed");
    static const uint8_t b3_data[] = {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45};
    for (size_t i = 0; i < sizeof(b3_data); ++i) {
        ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(b3_data[i]), TAG, "send B3 data failed");
    }

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xB4), TAG, "send 0xB4 failed");
    static const uint8_t b4_data[] = {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45};
    for (size_t i = 0; i < sizeof(b4_data); ++i) {
        ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(b4_data[i]), TAG, "send B4 data failed");
    }

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x62), TAG, "send 0x62 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x32), TAG, "send 0x32 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x03), TAG, "send 0x03 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x1F), TAG, "send 0x1F failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xB7), TAG, "send 0xB7 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x13), TAG, "send 0x13 failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xB0), TAG, "send 0xB0 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x64), TAG, "send 0x64 failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x11), TAG, "send sleep out failed");
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xC9), TAG, "send 0xC9 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x00), TAG, "send 0x00 failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x36), TAG, "send 0x36 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x48), TAG, "send rotation failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x3A), TAG, "send 0x3A failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x11), TAG, "send pixel format failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xB9), TAG, "send 0xB9 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x20), TAG, "send 0x20 failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xB8), TAG, "send 0xB8 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x29), TAG, "send 0x29 failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x21), TAG, "send invert on failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x2A), TAG, "send 0x2A failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x12), TAG, "send col start failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x2A), TAG, "send col end failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x2B), TAG, "send 0x2B failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x00), TAG, "send row start failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0xC7), TAG, "send row end failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x35), TAG, "send 0x35 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x00), TAG, "send tearing effect line failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0xD0), TAG, "send 0xD0 failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0xFF), TAG, "send 0xFF failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x38), TAG, "send idle off failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x29), TAG, "send display on failed");

    return ESP_OK;
}

esp_err_t rlcd_bsp_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    spi_bus_config_t bus_config = {
        .mosi_io_num = RLCD_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = RLCD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = RLCD_BSP_WIDTH * RLCD_BSP_HEIGHT,
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG, "spi bus init failed");

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = RLCD_DC_PIN,
        .cs_gpio_num = RLCD_CS_PIN,
        .pclk_hz = 20 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &s_io_handle),
        TAG,
        "panel io init failed");

    gpio_config_t rst_config = {
        .pin_bit_mask = 1ULL << RLCD_RST_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst_config), TAG, "reset gpio init failed");

    s_framebuffer_len = (RLCD_BSP_WIDTH * RLCD_BSP_HEIGHT) / 8;
    s_framebuffer = heap_caps_malloc(s_framebuffer_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_framebuffer == NULL) {
        ESP_LOGW(TAG, "PSRAM framebuffer alloc failed, fallback to internal RAM");
        s_framebuffer = heap_caps_malloc(s_framebuffer_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    } else {
        ESP_LOGI(TAG, "framebuffer 已分配到 PSRAM");
    }
    ESP_RETURN_ON_FALSE(s_framebuffer != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer alloc failed");

    rlcd_bsp_reset();
    ESP_RETURN_ON_ERROR(rlcd_bsp_run_init_sequence(), TAG, "panel init sequence failed");

    rlcd_bsp_fill(RLCD_BSP_COLOR_WHITE);
    s_initialized = true;
    ESP_LOGI(TAG, "RLCD 初始化完成，缓冲区大小=%u", (unsigned int)s_framebuffer_len);
    return ESP_OK;
}

void rlcd_bsp_fill(rlcd_bsp_color_t color)
{
    assert(s_framebuffer != NULL);
    memset(s_framebuffer, rlcd_bsp_raw_color(color), s_framebuffer_len);
}

void rlcd_bsp_draw_pixel(uint16_t x, uint16_t y, rlcd_bsp_color_t color)
{
    if (s_framebuffer == NULL || x >= RLCD_BSP_WIDTH || y >= RLCD_BSP_HEIGHT) {
        return;
    }

    uint16_t inv_y = RLCD_BSP_HEIGHT - 1 - y;
    uint16_t byte_x = x / 2;
    uint16_t block_y = inv_y / 4;
    uint32_t index = byte_x * (RLCD_BSP_HEIGHT / 4) + block_y;
    uint8_t local_x = x % 2;
    uint8_t local_y = inv_y % 4;
    uint8_t bit = 7 - (local_y * 2 + local_x);
    uint8_t mask = 1U << bit;

    if (color == RLCD_BSP_COLOR_WHITE) {
        s_framebuffer[index] |= mask;
    } else {
        s_framebuffer[index] &= (uint8_t)~mask;
    }
}

void rlcd_bsp_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, rlcd_bsp_color_t color, bool filled)
{
    if (width == 0 || height == 0) {
        return;
    }

    uint16_t x_end = x + width;
    uint16_t y_end = y + height;
    if (x_end > RLCD_BSP_WIDTH) {
        x_end = RLCD_BSP_WIDTH;
    }
    if (y_end > RLCD_BSP_HEIGHT) {
        y_end = RLCD_BSP_HEIGHT;
    }

    for (uint16_t py = y; py < y_end; ++py) {
        for (uint16_t px = x; px < x_end; ++px) {
            bool draw = filled || py == y || py == y_end - 1 || px == x || px == x_end - 1;
            if (draw) {
                rlcd_bsp_draw_pixel(px, py, color);
            }
        }
    }
}

esp_err_t rlcd_bsp_present(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "rlcd not initialized");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x2A), TAG, "set column failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x12), TAG, "set column start failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x2A), TAG, "set column end failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x2B), TAG, "set page failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0x00), TAG, "set page start failed");
    ESP_RETURN_ON_ERROR(rlcd_bsp_send_data(0xC7), TAG, "set page end failed");

    ESP_RETURN_ON_ERROR(rlcd_bsp_send_command(0x2C), TAG, "memory write failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(s_io_handle, -1, s_framebuffer, s_framebuffer_len), TAG, "push framebuffer failed");

    return ESP_OK;
}
