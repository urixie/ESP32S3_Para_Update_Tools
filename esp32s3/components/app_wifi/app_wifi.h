#ifndef APP_WIFI_H
#define APP_WIFI_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*app_wifi_ready_cb_t)(const char *ip_addr);

void app_wifi_set_ready_callback(app_wifi_ready_cb_t cb);
esp_err_t app_wifi_ap_start(void);

#ifdef __cplusplus
}
#endif

#endif
