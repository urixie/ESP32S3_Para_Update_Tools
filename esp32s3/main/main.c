#include "app_storage_lock.h"
#include "app_web_file_server.h"
#include "app_wifi.h"
#include "bsp.h"
#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "main";

static void restore_app_info_logs(void)
{
    esp_log_level_set("main", ESP_LOG_INFO);
    esp_log_level_set("app_wifi", ESP_LOG_INFO);
    esp_log_level_set("web_param", ESP_LOG_INFO);
    esp_log_level_set("param_board", ESP_LOG_INFO);
    esp_log_level_set("param_bin", ESP_LOG_INFO);
    esp_log_level_set("storage_lock", ESP_LOG_INFO);
    esp_log_level_set("storage", ESP_LOG_INFO);
    esp_log_level_set("STORAGE", ESP_LOG_INFO);
}

static esp_err_t start_file_server_on_ip(const char *ip_addr)
{
    esp_err_t ret = app_web_file_server_start(g_storage.disk_path);
    if (ret == ESP_OK) {
        char url[48];
        snprintf(url, sizeof(url), "http://%s/", ip_addr);
        ESP_LOGI(TAG, "Web parameter bin manager: %s", url);
    } else {
        ESP_LOGE(TAG, "web file server start failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

void app_main(void)
{
    restore_app_info_logs();

    esp_err_t ret = app_storage_lock_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "storage lock init failed: %s", esp_err_to_name(ret));
    }

    bsp_init();

    app_wifi_set_ready_callback(start_file_server_on_ip);
    ret = app_wifi_ap_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi/web parameter manager disabled: %s", esp_err_to_name(ret));
    }
}
