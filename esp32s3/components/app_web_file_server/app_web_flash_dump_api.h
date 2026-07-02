#ifndef APP_WEB_FLASH_DUMP_API_H
#define APP_WEB_FLASH_DUMP_API_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t flash_dump_start_handler(httpd_req_t *req);
esp_err_t flash_dump_status_handler(httpd_req_t *req);
esp_err_t flash_dump_data_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif
