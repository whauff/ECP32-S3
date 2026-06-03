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
#include "esp_http_server.h"
#include <ctype.h>

static const char *TAG = "wifi_time_sync";

static EventGroupHandle_t s_event_group;
static esp_netif_t *s_wifi_netif;
static esp_event_handler_instance_t s_wifi_any_handler;
static esp_event_handler_instance_t s_wifi_ip_handler;
static bool s_initialized;
static bool s_sntp_started;
static wifi_time_sync_status_t s_status;

static const char* index_html = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<style>"
"body { font-family: Arial, sans-serif; margin: 30px; background-color: #f0f2f5; text-align: center; }"
".card { background: white; padding: 25px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); display: inline-block; max-width: 320px; width: 100%; text-align: left; }"
"h2 { margin-top: 0; color: #333; text-align: center; }"
"input[type=text], input[type=password] { width: 100%; padding: 10px; margin: 8px 0 15px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }"
"button { background-color: #007bff; color: white; border: none; padding: 12px 20px; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px; }"
"button:hover { background-color: #0056b3; }"
"</style>"
"</head>"
"<body>"
"<div class=\"card\">"
"<h2>ECP32-S3 Wi-Fi Setup</h2>"
"<form action=\"/config\" method=\"post\">"
"<label>SSID</label>"
"<input type=\"text\" name=\"ssid\" required placeholder=\"Enter Wi-Fi Name\">"
"<label>Password</label>"
"<input type=\"password\" name=\"password\" placeholder=\"Enter Password\">"
"<button type=\"submit\">Connect</button>"
"</form>"
"</div>"
"</body>"
"</html>";

static httpd_handle_t s_server = NULL;

static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit((int)a) && isxdigit((int)b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static esp_err_t get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t post_handler(httpd_req_t *req) {
    char buf[256];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive post data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    char raw_ssid[64] = {0};
    char raw_pwd[64] = {0};
    char ssid[64] = {0};
    char pwd[64] = {0};
    
    char *p = strstr(buf, "ssid=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        if (end) {
            memcpy(raw_ssid, p, end - p);
        } else {
            strcpy(raw_ssid, p);
        }
    }
    
    p = strstr(buf, "password=");
    if (p) {
        p += 9;
        char *end = strchr(p, '&');
        if (end) {
            memcpy(raw_pwd, p, end - p);
        } else {
            strcpy(raw_pwd, p);
        }
    }
    
    url_decode(ssid, raw_ssid);
    url_decode(pwd, raw_pwd);
    
    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID cannot be empty");
        return ESP_FAIL;
    }
    
    nvs_handle_t my_handle;
    if (nvs_open("wifi_store", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "ssid", ssid);
        nvs_set_str(my_handle, "pswd", pwd);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    
    const char *resp = "<html><body><h2>Config received. Restarting to connect...</h2></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    
    ESP_LOGI("wifi_time_sync", "Web Portal 配网成功，SSID=%s，1.5秒后自动重启...", ssid);
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

static void wifi_time_sync_stop_webserver(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

static void wifi_time_sync_start_webserver(void) {
    if (s_server) return;
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    
    httpd_uri_t get_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = get_handler,
        .user_ctx = NULL
    };
    httpd_uri_t post_uri = {
        .uri      = "/config",
        .method   = HTTP_POST,
        .handler  = post_handler,
        .user_ctx = NULL
    };
    
    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_register_uri_handler(s_server, &get_uri);
        httpd_register_uri_handler(s_server, &post_uri);
        ESP_LOGI("wifi_time_sync", "Web Server 启动成功，端口: 80");
    } else {
        ESP_LOGE("wifi_time_sync", "Web Server 启动失败");
    }
}

static void wifi_time_sync_start_ap_portal(void) {
    s_status.wifi_config_mode = true;
    s_status.wifi_connected = false;
    
    esp_wifi_stop();
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ECP32-S3-Config",
            .ssid_len = strlen("ECP32-S3-Config"),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
    
    ESP_LOGI("wifi_time_sync", "AP 配网热点已开启，SSID: ECP32-S3-Config");
    wifi_time_sync_start_webserver();
}

static bool wifi_time_sync_read_nvs(char *ssid, char *pswd) {
    nvs_handle_t my_handle;
    esp_err_t err;
    bool success = false;
    
    err = nvs_open("wifi_store", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t ssid_len = 64;
        size_t pswd_len = 64;
        esp_err_t err_s = nvs_get_str(my_handle, "ssid", ssid, &ssid_len);
        esp_err_t err_p = nvs_get_str(my_handle, "pswd", pswd, &pswd_len);
        if (err_s == ESP_OK && err_p == ESP_OK) {
            success = true;
        }
        nvs_close(my_handle);
    }
    return success;
}

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1

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
        // 只有在非 AP 模式下才主动触发 STA 连接
        if (!s_status.wifi_config_mode) {
            esp_wifi_connect();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_status.wifi_connected = false;
        if (!s_status.wifi_config_mode) {
            if (s_status.retry_count < CONFIG_BOARD_WIFI_MAXIMUM_RETRY) {
                s_status.retry_count++;
                ESP_LOGW(TAG, "Wi-Fi 断开，开始重连 (%" PRIu32 "/%d)",
                         s_status.retry_count, CONFIG_BOARD_WIFI_MAXIMUM_RETRY);
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_event_group, WIFI_FAILED_BIT);
                ESP_LOGW(TAG, "Wi-Fi 重试次数已用尽，启动网页配网");
                wifi_time_sync_start_ap_portal();
            }
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_status.wifi_connected = true;
        s_status.retry_count = 0;
        s_status.wifi_config_mode = false;
        wifi_time_sync_stop_webserver();
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
    
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(ap_netif != NULL, ESP_ERR_NO_MEM, TAG, "AP netif 创建失败");

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

    char ssid[64] = {0};
    char pswd[64] = {0};
    bool has_nvs = wifi_time_sync_read_nvs(ssid, pswd);

    if (has_nvs && strlen(ssid) > 0) {
        s_status.wifi_configured = true;
        ESP_LOGI(TAG, "从 NVS 中成功读取到 Wi-Fi，SSID=%s", ssid);
    } else {
        if (strlen(CONFIG_BOARD_WIFI_SSID) > 0) {
            strncpy(ssid, CONFIG_BOARD_WIFI_SSID, sizeof(ssid) - 1);
            strncpy(pswd, CONFIG_BOARD_WIFI_PASSWORD, sizeof(pswd) - 1);
            s_status.wifi_configured = true;
            ESP_LOGI(TAG, "NVS 无配置，使用静态配置，SSID=%s", ssid);
        } else {
            s_status.wifi_configured = false;
        }
    }

    if (s_status.wifi_configured) {
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, pswd, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = strlen(pswd) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi 模式设置失败");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Wi-Fi 配置失败");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi 启动失败");
        ESP_LOGI(TAG, "Wi-Fi STA 模式已启动，正在连接...");
    } else {
        ESP_LOGW(TAG, "无可用 Wi-Fi 配置，直接启动 AP 网页配网模式");
        wifi_time_sync_start_ap_portal();
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_time_sync_get_status(wifi_time_sync_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, TAG, "输出参数为空");
    *out_status = s_status;
    return ESP_OK;
}

void wifi_time_sync_power_save(void)
{
    esp_wifi_stop();
    s_status.wifi_connected = false;
    s_status.wifi_configured = false;
    s_status.wifi_config_mode = false;
    ESP_LOGI(TAG, "已彻底关闭 Wi-Fi 射频以节省电能");
}
