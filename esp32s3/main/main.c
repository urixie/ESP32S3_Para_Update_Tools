
#include "app_storage_lock.h"
#include "app_web_file_server.h"
#include "app_wifi.h"
#include "bsp.h"
#include "esp_log.h"

static const char *TAG = "main";

static esp_err_t start_file_server_on_ip(const char *ip_addr)
{
    esp_err_t ret = app_web_file_server_start(g_storage.disk_path);
    if (ret == ESP_OK) {
        char url[48];
        snprintf(url, sizeof(url), "http://%s/", ip_addr);
        ESP_LOGI(TAG, "Web file manager: %s", url);
    } else {
        ESP_LOGE(TAG, "web file server start failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

void app_main(void)
{
    esp_err_t ret = app_storage_lock_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "storage lock init failed: %s", esp_err_to_name(ret));
    }

    bsp_init();

    app_wifi_set_got_ip_callback(start_file_server_on_ip);
    ret = app_wifi_sta_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi/web file manager disabled: %s", esp_err_to_name(ret));
    }
}
