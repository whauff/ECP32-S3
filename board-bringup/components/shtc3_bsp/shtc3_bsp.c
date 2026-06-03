#include "shtc3_bsp.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHTC3_BSP_I2C_PORT I2C_NUM_0
#define SHTC3_BSP_I2C_ADDR 0x70
#define SHTC3_BSP_FREQ_HZ 400000

#define SHTC3_CMD_READ_ID 0xEFC8
#define SHTC3_CMD_WAKEUP 0x3517
#define SHTC3_CMD_SLEEP 0xB098
#define SHTC3_CMD_SOFT_RESET 0x805D
#define SHTC3_CMD_MEASURE_POLLING 0x7866

static const char *TAG = "shtc3_bsp";
static i2c_master_dev_handle_t s_shtc3_dev;
static bool s_initialized;

static esp_err_t shtc3_bsp_write_cmd(uint16_t cmd)
{
    const uint8_t payload[2] = {
        (uint8_t)(cmd >> 8),
        (uint8_t)(cmd & 0xFF),
    };
    return i2c_master_transmit(s_shtc3_dev, payload, sizeof(payload), -1);
}

static uint8_t shtc3_bsp_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t shtc3_bsp_check_crc(const uint8_t *data, size_t len, uint8_t checksum)
{
    return (shtc3_bsp_crc8(data, len) == checksum) ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t shtc3_bsp_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus_handle = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(SHTC3_BSP_I2C_PORT, &bus_handle), TAG, "I2C 总线未就绪");

    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_BSP_I2C_ADDR,
        .scl_speed_hz = SHTC3_BSP_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_config, &s_shtc3_dev), TAG, "SHTC3 设备挂载失败");
    ESP_RETURN_ON_ERROR(shtc3_bsp_write_cmd(SHTC3_CMD_WAKEUP), TAG, "SHTC3 唤醒失败");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_RETURN_ON_ERROR(shtc3_bsp_write_cmd(SHTC3_CMD_SOFT_RESET), TAG, "SHTC3 复位失败");
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t read_id_cmd[2] = {
        (uint8_t)(SHTC3_CMD_READ_ID >> 8),
        (uint8_t)(SHTC3_CMD_READ_ID & 0xFF),
    };
    uint8_t id_buf[3] = {0};
    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(s_shtc3_dev, read_id_cmd, sizeof(read_id_cmd), id_buf, sizeof(id_buf), -1),
        TAG,
        "SHTC3 读取 ID 失败");
    ESP_RETURN_ON_ERROR(shtc3_bsp_check_crc(id_buf, 2, id_buf[2]), TAG, "SHTC3 ID CRC 错误");

    ESP_LOGI(TAG, "SHTC3 已就绪，ID=0x%02X%02X", id_buf[0], id_buf[1]);
    s_initialized = true;
    return ESP_OK;
}

esp_err_t shtc3_bsp_read(shtc3_bsp_reading_t *out_reading)
{
    uint8_t data[6] = {0};
    uint16_t raw_temp = 0;
    uint16_t raw_humi = 0;

    ESP_RETURN_ON_FALSE(out_reading != NULL, ESP_ERR_INVALID_ARG, TAG, "输出参数为空");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "SHTC3 未初始化");

    memset(out_reading, 0, sizeof(*out_reading));

    ESP_RETURN_ON_ERROR(shtc3_bsp_write_cmd(SHTC3_CMD_WAKEUP), TAG, "SHTC3 唤醒失败");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(shtc3_bsp_write_cmd(SHTC3_CMD_MEASURE_POLLING), TAG, "SHTC3 触发采样失败");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(i2c_master_receive(s_shtc3_dev, data, sizeof(data), -1), TAG, "SHTC3 读取数据失败");
    ESP_RETURN_ON_ERROR(shtc3_bsp_write_cmd(SHTC3_CMD_SLEEP), TAG, "SHTC3 休眠失败");

    ESP_RETURN_ON_ERROR(shtc3_bsp_check_crc(data, 2, data[2]), TAG, "SHTC3 温度 CRC 错误");
    ESP_RETURN_ON_ERROR(shtc3_bsp_check_crc(&data[3], 2, data[5]), TAG, "SHTC3 湿度 CRC 错误");

    raw_temp = (uint16_t)((data[0] << 8) | data[1]);
    raw_humi = (uint16_t)((data[3] << 8) | data[4]);

    out_reading->temperature_c = 175.0f * (float)raw_temp / 65536.0f - 45.0f;
    out_reading->humidity_percent = 100.0f * (float)raw_humi / 65536.0f;
    out_reading->valid = true;
    return ESP_OK;
}
