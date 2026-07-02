#ifndef APP_WEB_PAGES_H
#define APP_WEB_PAGES_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t index_handler(httpd_req_t *req);
esp_err_t app_handler(httpd_req_t *req);
esp_err_t app_info_handler(httpd_req_t *req);
esp_err_t favicon_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif
