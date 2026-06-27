#include "app_storage_lock.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "storage_lock";
static SemaphoreHandle_t s_fs_mutex;

esp_err_t app_storage_lock_init(void)
{
    if (s_fs_mutex != NULL) {
        return ESP_OK;
    }

    s_fs_mutex = xSemaphoreCreateMutex();
    if (s_fs_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create filesystem mutex");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "filesystem mutex initialized");
    return ESP_OK;
}

esp_err_t app_storage_lock(TickType_t ticks_to_wait)
{
    if (s_fs_mutex == NULL) {
        esp_err_t ret = app_storage_lock_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (xSemaphoreTake(s_fs_mutex, ticks_to_wait) != pdTRUE) {
        ESP_LOGW(TAG, "filesystem lock timeout");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGD(TAG, "filesystem locked");
    return ESP_OK;
}

void app_storage_unlock(void)
{
    if (s_fs_mutex == NULL) {
        return;
    }
    xSemaphoreGive(s_fs_mutex);
    ESP_LOGD(TAG, "filesystem unlocked");
}
