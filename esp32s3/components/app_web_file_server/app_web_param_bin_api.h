#ifndef APP_WEB_PARAM_BIN_API_H
#define APP_WEB_PARAM_BIN_API_H

#include <stddef.h>

#include "app_param_bin.h"
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t load_parsed_bin(const char *encoded_path,
                          app_param_bin_result_t **out,
                          char *err_msg,
                          size_t err_msg_size);
esp_err_t bin_parse_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif
