#include "app_wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define APP_WIFI_AP_SSID "Uniedge驱动器参数更新"
#define APP_WIFI_AP_PASSWORD "12345678"
#define APP_WIFI_AP_CHANNEL 6
#define APP_WIFI_AP_MAX_CONN 4

static const char *TAG = "app_wifi";
static bool s_started;
static esp_netif_t *s_ap_netif;
static app_wifi_ready_cb_t s_ready_cb;

void app_wifi_set_ready_callback(app_wifi_ready_cb_t cb)
{
    s_ready_cb = cb;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " joined, aid=%d", MAC2STR(event->mac), event->aid);
        return;
    }

    if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " left, aid=%d", MAC2STR(event->mac), event->aid);
    }
}

esp_err_t app_wifi_ap_start(void)
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

    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_ap_netif != NULL, ESP_FAIL, TAG, "create default wifi ap failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler, NULL, NULL),
                        TAG, "register wifi event failed");

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.ap.ssid, APP_WIFI_AP_SSID, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(APP_WIFI_AP_SSID);
    strlcpy((char *)wifi_config.ap.password, APP_WIFI_AP_PASSWORD, sizeof(wifi_config.ap.password));
    wifi_config.ap.channel = APP_WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = APP_WIFI_AP_MAX_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set ap mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "set ap config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi ap start failed");

    esp_netif_ip_info_t ip_info;
    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(s_ap_netif, &ip_info), TAG, "get ap ip failed");
    char ip_addr[16];
    esp_ip4addr_ntoa(&ip_info.ip, ip_addr, sizeof(ip_addr));

    s_started = true;
    ESP_LOGI(TAG, "WiFi SoftAP started, ssid=%s, password=%s, channel=%d",
             APP_WIFI_AP_SSID, APP_WIFI_AP_PASSWORD, APP_WIFI_AP_CHANNEL);
    ESP_LOGI(TAG, "AP address: %s", ip_addr);

    if (s_ready_cb != NULL) {
        ret = s_ready_cb(ip_addr);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ready callback failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    return ESP_OK;
}
