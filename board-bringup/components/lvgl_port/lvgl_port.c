#include "lvgl_port.h"

#include <assert.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rlcd_bsp.h"

#define LVGL_TICK_PERIOD_MS 5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 16
#define LVGL_DRAW_BUFFER_LINES 40

static const char *TAG = "lvgl_port";
static SemaphoreHandle_t s_lvgl_mutex;
static lv_display_t *s_display;
static esp_timer_handle_t s_tick_timer;
static bool s_initialized;

static void lvgl_port_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *arg)
{
    (void)arg;
    uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;

    while (true) {
        if (lvgl_port_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }

        if (delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static inline rlcd_bsp_color_t lvgl_port_color_to_bw(uint16_t color)
{
    uint8_t r = (uint8_t)((color >> 11) & 0x1F);
    uint8_t g = (uint8_t)((color >> 5) & 0x3F);
    uint8_t b = (uint8_t)(color & 0x1F);
    uint16_t luma = (uint16_t)(r * 299U + g * 587U + b * 114U);
    return luma >= 16000U ? RLCD_BSP_COLOR_WHITE : RLCD_BSP_COLOR_BLACK;
}

static void lvgl_port_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *color_map)
{
    uint16_t *buffer = (uint16_t *)color_map;

    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            rlcd_bsp_draw_pixel((uint16_t)x, (uint16_t)y, lvgl_port_color_to_bw(*buffer));
            ++buffer;
        }
    }

    if (lv_display_flush_is_last(display)) {
        esp_err_t err = rlcd_bsp_present();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "提交屏幕缓冲失败: %s", esp_err_to_name(err));
        }
    }

    lv_display_flush_ready(display);
}

bool lvgl_port_lock(int timeout_ms)
{
    TickType_t ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mutex, ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(s_lvgl_mutex != NULL);
    xSemaphoreGive(s_lvgl_mutex);
}

esp_err_t lvgl_port_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_lvgl_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lvgl_mutex != NULL, ESP_ERR_NO_MEM, TAG, "创建 LVGL 互斥锁失败");

    lv_init();

    s_display = lv_display_create(RLCD_BSP_WIDTH, RLCD_BSP_HEIGHT);
    ESP_RETURN_ON_FALSE(s_display != NULL, ESP_FAIL, TAG, "创建 LVGL display 失败");

    size_t buffer_size = RLCD_BSP_WIDTH * LVGL_DRAW_BUFFER_LINES * sizeof(uint16_t);
    void *buffer1 = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *buffer2 = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(buffer1 != NULL && buffer2 != NULL, ESP_ERR_NO_MEM, TAG, "分配 LVGL 缓冲失败");

    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, lvgl_port_flush_cb);
    lv_display_set_buffers(s_display, buffer1, buffer2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_port_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &s_tick_timer), TAG, "创建 LVGL tick 定时器失败");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_tick_timer, LVGL_TICK_PERIOD_MS * 1000), TAG, "启动 LVGL tick 定时器失败");

    BaseType_t ok = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl_task", 8192, NULL, 5, NULL, 0);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_FAIL, TAG, "创建 LVGL 任务失败");

    s_initialized = true;
    ESP_LOGI(TAG, "LVGL 初始化完成");
    return ESP_OK;
}
