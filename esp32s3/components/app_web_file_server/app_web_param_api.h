#ifndef APP_WEB_PARAM_API_H
#define APP_WEB_PARAM_API_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "app_param_board.h"


#ifdef __cplusplus
extern "C" {
#endif

const char *param_board_state_name(app_param_board_op_state_t state);
esp_err_t send_param_board_started_json(httpd_req_t *req, uint32_t op_id);

esp_err_t bin_parse_handler(httpd_req_t *req);
esp_err_t param_readback_handler(httpd_req_t *req);
esp_err_t param_connect_cancel_handler(httpd_req_t *req);
esp_err_t param_status_handler(httpd_req_t *req);
esp_err_t param_download_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif
