#ifndef APP_WEB_AUTH_H
#define APP_WEB_AUTH_H

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEB_AUTH_ROLE_NONE = 0,
    WEB_AUTH_ROLE_USER,
    WEB_AUTH_ROLE_ADMIN,
} web_auth_role_t;

bool web_auth_get_session(httpd_req_t *req, web_auth_role_t *role);
bool web_auth_is_logged_in(httpd_req_t *req);
esp_err_t require_logged_in(httpd_req_t *req, web_auth_role_t *role);
esp_err_t require_admin(httpd_req_t *req);

esp_err_t login_handler(httpd_req_t *req);
esp_err_t logout_handler(httpd_req_t *req);
esp_err_t auth_status_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif
