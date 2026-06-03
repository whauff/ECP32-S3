#include "wifi_time_sync.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "lwip/ip_addr.h"
#include "nvs_flash.h"
#include "rtc_bsp.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1

static const char *TAG = "wifi_time_sync";

static EventGroupHandle_t s_event_group;
static esp_netif_t *s_wifi_netif;
static esp_event_handler_instance_t s_wifi_any_handler;
static esp_event_handler_instance_t s_wifi_ip_handler;
static bool s_initialized;
static bool s_sntp_started;
static wifi_time_sync_status_t s_status;

static bool wifi_time_sync_has_ssid(void)
{
    return strlen(CONFIG_BOARD_WIFI_SSID) > 0;
}

static bool wifi_time_sync_time_is_reasonable(const struct tm *timeinfo)
{
    return timeinfo != NULL && timeinfo->tm_year >= (2024 - 1900);
}

static esp_err_t wifi_time_sync_write_back_rtc(void)
{
    time_t now = 0;
    struct tm local_time = {0};
    rtc_bsp_datetime_t rtc_time = {0};

    time(&now);
    localtime_r(&now, &local_time);
    ESP_RETURN_ON_FALSE(wifi_time_sync_time_is_reasonable(&local_time), ESP_ERR_INVALID_STATE, TAG, "系统时间仍未就绪");

    rtc_time.year = (uint16_t)(local_time.tm_year + 1900);
    rtc_time.month = (uint8_t)(local_time.tm_mon + 1);
    rtc_time.day = (uint8_t)local_time.tm_mday;
    rtc_time.hour = (uint8_t)local_time.tm_hour;
    rtc_time.minute = (uint8_t)local_time.tm_min;
    rtc_time.second = (uint8_t)local_time.tm_sec;
    rtc_time.valid = true;

    ESP_RETURN_ON_ERROR(rtc_bsp_set_datetime(&rtc_time), TAG, "RTC 回写失败");
    ESP_LOGI(TAG, "NTP 校时成功，RTC 已回写为 %04u-%02u-%02u %02u:%02u:%02u",
             rtc_time.year, rtc_time.month, rtc_time.day,
             rtc_time.hour, rtc_time.minute, rtc_time.second);
    return ESP_OK;
}

static void wifi_time_sync_on_ntp(struct timeval *tv)
{
    (void)tv;
    if (wifi_time_sync_write_back_rtc() == ESP_OK) {
        s_status.time_synced = true;
    }
}

static void wifi_time_sync_start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }

    setenv("TZ", CONFIG_BOARD_TIMEZONE, 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_BOARD_NTP_SERVER);
    config.sync_cb = wifi_time_sync_on_ntp;
    esp_netif_sntp_init(&config);
    s_sntp_started = true;

    ESP_LOGI(TAG, "SNTP 已启动，服务器=%s，时区=%s", CONFIG_BOARD_NTP_SERVER, CONFIG_BOARD_TIMEZONE);
}

static void wifi_time_sync_event_handler(void *arg,
                                         esp_event_base_t event_base,
                                         int32_t event_id,
                                         void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_status.wifi_connected = false;
        if (s_status.retry_count < CONFIG_BOARD_WIFI_MAXIMUM_RETRY) {
            s_status.retry_count++;
            ESP_LOGW(TAG, "Wi-Fi 断开，开始重连 (%" PRIu32 "/%d)",
                     s_status.retry_count, CONFIG_BOARD_WIFI_MAXIMUM_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_event_group, WIFI_FAILED_BIT);
            ESP_LOGW(TAG, "Wi-Fi 重试次数已用尽");
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_status.wifi_connected = true;
        s_status.retry_count = 0;
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi 已联网，IP=" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_time_sync_start_sntp();
    }
}

static esp_err_t wifi_time_sync_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS 擦除失败");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t wifi_time_sync_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_status.wifi_configured = wifi_time_sync_has_ssid();
    if (!s_status.wifi_configured) {
        ESP_LOGW(TAG, "未配置 Wi-Fi SSID，跳过联网校时");
        s_initialized = true;
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(wifi_time_sync_init_nvs(), TAG, "NVS 初始化失败");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif 初始化失败");

    esp_err_t event_loop_err = esp_event_loop_create_default();
    if (event_loop_err != ESP_OK && event_loop_err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(event_loop_err, TAG, "事件循环初始化失败");
    }

    s_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_event_group != NULL, ESP_ERR_NO_MEM, TAG, "事件组创建失败");

    s_wifi_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_wifi_netif != NULL, ESP_ERR_NO_MEM, TAG, "STA netif 创建失败");

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi 初始化失败");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_time_sync_event_handler,
                                                            NULL,
                                                            &s_wifi_any_handler),
                        TAG, "Wi-Fi 事件注册失败");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &wifi_time_sync_event_handler,
                                                            NULL,
                                                            &s_wifi_ip_handler),
                        TAG, "IP 事件注册失败");

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_BOARD_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_BOARD_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(CONFIG_BOARD_WIFI_PASSWORD) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi 模式设置失败");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Wi-Fi 配置失败");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi 启动失败");

    s_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi 校时模块已启动，SSID=%s", CONFIG_BOARD_WIFI_SSID);
    return ESP_OK;
}

esp_err_t wifi_time_sync_get_status(wifi_time_sync_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, TAG, "输出参数为空");
    *out_status = s_status;
    return ESP_OK;
}
