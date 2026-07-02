#ifndef APP_WEB_ASSETS_H
#define APP_WEB_ASSETS_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t assets_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif
