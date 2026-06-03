#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool wifi_configured;
    bool wifi_connected;
    bool wifi_config_mode;
    bool time_synced;
    uint32_t retry_count;
} wifi_time_sync_status_t;

esp_err_t wifi_time_sync_init(void);
esp_err_t wifi_time_sync_get_status(wifi_time_sync_status_t *out_status);
void wifi_time_sync_power_save(void);
void wifi_time_sync_start_wifi(void);

void wifi_time_sync_record_activity(void);
uint32_t wifi_time_sync_get_last_activity_ms(void);

#ifdef __cplusplus
}
#endif

