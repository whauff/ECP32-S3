#include "ui_home.h"

#include <stdio.h>

#include "lvgl.h"

static lv_obj_t *s_time_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_metrics_label;
static lv_obj_t *s_desc_label;

void ui_home_init(void)
{
    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "ECP32-S3");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 14);

    s_date_label = lv_label_create(screen);
    lv_label_set_text(s_date_label, "----.--.--");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_LEFT, 20, 44);

    s_battery_label = lv_label_create(screen);
    lv_label_set_text(s_battery_label, "BAT --%");
    lv_obj_set_style_text_font(s_battery_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -18, 18);

    s_time_label = lv_label_create(screen);
    lv_label_set_text(s_time_label, "--:--:--");
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -38);

    lv_obj_t *status_card = lv_obj_create(screen);
    lv_obj_set_size(status_card, 336, 128);
    lv_obj_align(status_card, LV_ALIGN_CENTER, 0, 86);
    lv_obj_set_style_radius(status_card, 0, 0);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(status_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(status_card, 2, 0);
    lv_obj_set_style_shadow_width(status_card, 0, 0);
    lv_obj_set_style_pad_all(status_card, 12, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    s_status_label = lv_label_create(status_card);
    lv_label_set_text(s_status_label, "LIVE STATUS");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_22, 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_LEFT, 12, 8);

    s_metrics_label = lv_label_create(status_card);
    lv_label_set_text(s_metrics_label, "TEMP --.- C\nHUM  --.- %RH\nVBAT -.-- V");
    lv_obj_set_style_text_font(s_metrics_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_line_space(s_metrics_label, 8, 0);
    lv_obj_align(s_metrics_label, LV_ALIGN_TOP_LEFT, 14, 42);

    s_desc_label = lv_label_create(screen);
    lv_label_set_text(s_desc_label, "WAITING FOR DATA");
    lv_obj_set_style_text_font(s_desc_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_desc_label, LV_ALIGN_BOTTOM_MID, 0, -18);
}

void ui_home_update(const ui_home_state_t *state)
{
    char date_text[24] = {0};
    char time_text[16] = {0};
    char battery_text[16] = {0};
    char metrics_text[64] = {0};
    char desc_text[48] = {0};

    if (state == NULL || s_time_label == NULL || s_date_label == NULL || s_battery_label == NULL ||
        s_metrics_label == NULL || s_desc_label == NULL) {
        return;
    }

    if (state->year > 0) {
        snprintf(date_text, sizeof(date_text), "%04u.%02u.%02u", state->year, state->month, state->day);
    } else {
        snprintf(date_text, sizeof(date_text), "----.--.--");
    }
    snprintf(time_text, sizeof(time_text), "%02u:%02u:%02u", state->hour, state->minute, state->second);
    snprintf(battery_text, sizeof(battery_text), "BAT %3u%%", state->battery_percent);
    if (state->climate_valid) {
        snprintf(metrics_text, sizeof(metrics_text), "TEMP %.1f C\nHUM  %.1f %%RH\nVBAT %.2f V",
                 state->temperature_c,
                 state->humidity_percent,
                 state->battery_voltage);
    } else {
        snprintf(metrics_text, sizeof(metrics_text), "TEMP --.- C\nHUM  --.- %%RH\nVBAT %.2f V",
                 state->battery_voltage);
    }
    snprintf(desc_text, sizeof(desc_text), "RTC: %s | WIFI: %s | NTP: %s",
             state->rtc_valid ? "OK" : "ERR",
             state->wifi_configured ? (state->wifi_connected ? "OK" : "WAIT") : "OFF",
             state->ntp_synced ? "OK" : "WAIT");

    lv_label_set_text(s_date_label, date_text);
    lv_label_set_text(s_time_label, time_text);
    lv_label_set_text(s_battery_label, battery_text);
    lv_label_set_text(s_metrics_label, metrics_text);
    lv_label_set_text(s_desc_label, desc_text);
}
