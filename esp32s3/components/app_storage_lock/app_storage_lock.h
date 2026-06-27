#ifndef APP_STORAGE_LOCK_H
#define APP_STORAGE_LOCK_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_storage_lock_init(void);
esp_err_t app_storage_lock(TickType_t ticks_to_wait);
void app_storage_unlock(void);

#ifdef __cplusplus
}
#endif

#endif
