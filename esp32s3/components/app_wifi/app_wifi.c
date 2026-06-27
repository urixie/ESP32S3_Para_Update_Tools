#include "app_wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define APP_WIFI_SSID "xyj-iPhone17"
#define APP_WIFI_PASSWORD "12345678"
#define APP_WIFI_CONNECTED_BIT BIT0

// 退避重连：避免紧贴断开重连导致状态机抖动 / 触发 AP 端限流
#define APP_WIFI_RETRY_DELAY_MS  3000
// 单次上电最大重试次数（达到后停止重连，等待外部动作）
#define APP_WIFI_MAX_RETRIES     10

static const char *TAG = "app_wifi";
static EventGroupHandle_t s_wifi_event_group;
static bool s_started;
static int s_retry_count;
static app_wifi_got_ip_cb_t s_got_ip_cb;

void app_wifi_set_got_ip_callback(app_wifi_got_ip_cb_t cb)
{
    s_got_ip_cb = cb;
}

/**
 * @brief 把 wifi_event_sta_disconnected_t::reason 转成可读字符串。
 *        关键诊断信息：密码错、找不到 AP、握手超时 等都能直接看到。
 */
static const char *wifi_disconnect_reason_str(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:        return "NO_AP_FOUND(201) - 没扫到目标SSID/频段不匹配";
    case WIFI_REASON_AUTH_FAIL:          return "AUTH_FAIL(202) - 密码错或加密不匹配";
    case WIFI_REASON_ASSOC_FAIL:         return "ASSOC_FAIL(203)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:  return "HANDSHAKE_TIMEOUT(204)";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT(205) - 密码错";
    case WIFI_REASON_CONNECTION_FAIL:    return "CONNECTION_FAIL";
    case WIFI_REASON_BEACON_TIMEOUT:     return "BEACON_TIMEOUT - 信号不稳";
    case WIFI_REASON_MIC_FAILURE:        return "MIC_FAILURE";
    case WIFI_REASON_NOT_AUTHED:         return "NOT_AUTHED";
    default:                             return "OTHER";
    }
}

static void wifi_retry_connect(void)
{
    if (s_retry_count >= APP_WIFI_MAX_RETRIES) {
        ESP_LOGE(TAG, "Reached max retries (%d), stop reconnecting. Check AP availability / password / channel band.", APP_WIFI_MAX_RETRIES);
        return;
    }
    s_retry_count++;
    ESP_LOGW(TAG, "Reconnecting to %s (attempt %d/%d) after %d ms", APP_WIFI_SSID, s_retry_count, APP_WIFI_MAX_RETRIES, APP_WIFI_RETRY_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(APP_WIFI_RETRY_DELAY_MS));
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA connecting to %s", APP_WIFI_SSID);
        s_retry_count = 0;
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn =
            (wifi_event_sta_disconnected_t *)event_data;
        xEventGroupClearBits(s_wifi_event_group, APP_WIFI_CONNECTED_BIT);

        // 关键：打印 SSID + BSSID + RSSI + 原因码，才能定位
        char bssid_str[18];
        snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 disconn->bssid[0], disconn->bssid[1], disconn->bssid[2],
                 disconn->bssid[3], disconn->bssid[4], disconn->bssid[5]);
        ESP_LOGW(TAG, "WiFi disconnected from ssid=%s bssid=%s rssi=%d reason=%s",
                 (char *)disconn->ssid, bssid_str,
                 disconn->rssi, wifi_disconnect_reason_str(disconn->reason));
        wifi_retry_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        // 扫描完成时打印一份扫描结果，验证目标 SSID 是否可见
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        if (ap_count == 0) {
            ESP_LOGW(TAG, "WiFi scan: 0 APs found");
            return;
        }
        wifi_ap_record_t *ap_list = calloc(ap_count, sizeof(*ap_list));
        if (ap_list == NULL) {
            return;
        }
        if (esp_wifi_scan_get_ap_records(&ap_count, ap_list) == ESP_OK) {
            bool target_found = false;
            for (int i = 0; i < ap_count; i++) {
                const char *auth = "OPEN";
                if (ap_list[i].authmode == WIFI_AUTH_WPA2_PSK)  auth = "WPA2_PSK";
                else if (ap_list[i].authmode == WIFI_AUTH_WPA3_PSK) auth = "WPA3_PSK";
                else if (ap_list[i].authmode == WIFI_AUTH_WPA2_WPA3_PSK) auth = "WPA2/WPA3";
                if (strcmp((char *)ap_list[i].ssid, APP_WIFI_SSID) == 0) {
                    target_found = true;
                    ESP_LOGI(TAG, "scan: TARGET '%s' ch=%d rssi=%d auth=%s",
                             APP_WIFI_SSID, ap_list[i].primary,
                             ap_list[i].rssi, auth);
                }
            }
            if (!target_found) {
                ESP_LOGE(TAG, "scan: target SSID '%s' NOT visible. "
                              "Check: 2.4GHz 是否开启 / iPhone 个人热点是否启用 / SSID 大小写",
                         APP_WIFI_SSID);
            }
        }
        free(ap_list);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip_addr[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_addr, sizeof(ip_addr));

        s_retry_count = 0;
        ESP_LOGI(TAG, "WiFi connected");
        ESP_LOGI(TAG, "IP address: %s", ip_addr);
        xEventGroupSetBits(s_wifi_event_group, APP_WIFI_CONNECTED_BIT);

        if (s_got_ip_cb != NULL) {
            esp_err_t ret = s_got_ip_cb(ip_addr);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "got-ip callback failed: %s", esp_err_to_name(ret));
            }
        }
    }
}

esp_err_t app_wifi_sta_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs init failed");
    ESP_LOGI(TAG, "NVS initialized");

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "esp_netif_init failed");
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "esp_event_loop_create_default failed");
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "wifi event group alloc failed");

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler, NULL, NULL),
                        TAG, "register wifi event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            &wifi_event_handler, NULL, NULL),
                        TAG, "register ip event failed");

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, APP_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, APP_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    // authmode 是"最低可接受"认证等级：设为 WPA2_PSK 即允许 WPA2 与 WPA3，
    // 但 iPhone 热点某些 iOS 版本仅启用 WPA3 SAE，留意日志 reason 码。
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set sta mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set sta config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    // 启动后做一次主动扫描，便于排查 SSID 不可见 / 频段不匹配问题
    wifi_scan_config_t scan_cfg = {0};
    scan_cfg.ssid = (uint8_t *)APP_WIFI_SSID;
    scan_cfg.show_hidden = false;
    esp_err_t scan_ret = esp_wifi_scan_start(&scan_cfg, false);
    if (scan_ret != ESP_OK) {
        ESP_LOGW(TAG, "initial scan failed: %s", esp_err_to_name(scan_ret));
    }

    s_started = true;
    return ESP_OK;
}
