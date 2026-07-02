#ifndef APP_WEB_FILES_H
#define APP_WEB_FILES_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t files_handler(httpd_req_t *req);
esp_err_t download_handler(httpd_req_t *req);
esp_err_t upload_handler(httpd_req_t *req);
esp_err_t delete_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif
