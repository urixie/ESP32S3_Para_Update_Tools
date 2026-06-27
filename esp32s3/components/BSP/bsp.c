#include "bsp.h"

#include "esp_log.h"

static const char *TAG = "STORAGE";

void bsp_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Mount storage at %s", g_storage.disk_path);
    ESP_ERROR_CHECK(storage_flash_mount(g_storage.disk_path));
}
