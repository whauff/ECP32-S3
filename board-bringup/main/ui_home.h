#pragma once

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t battery_percent;
    float battery_voltage;
    float temperature_c;
    float humidity_percent;
    int rtc_valid;
    int climate_valid;
    int wifi_configured;
    int wifi_connected;
    int ntp_synced;
} ui_home_state_t;

void ui_home_init(void);
void ui_home_update(const ui_home_state_t *state);
