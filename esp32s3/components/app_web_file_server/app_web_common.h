#ifndef APP_WEB_COMMON_H
#define APP_WEB_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_WEB_TAG "web_param"
#define APP_WEB_MAX_PATH 320
#define APP_WEB_IO_BUF_SIZE 2048
#define APP_WEB_UPLOAD_BUF_SIZE 4096
#define APP_WEB_PARAM_FORM_BUF_SIZE 1536
#define APP_WEB_PARAM_TIME_BASE_NS 50U
#define APP_WEB_PARAM_MAX_NS_VALUE ((uint32_t)UINT16_MAX * APP_WEB_PARAM_TIME_BASE_NS)

esp_err_t app_web_set_mount_point(const char *mount_point);
const char *app_web_mount_point(void);

void log_heap_state(const char *where);
void set_connection_close(httpd_req_t *req);
void set_no_store(httpd_req_t *req);
esp_err_t url_decode(char *dst, size_t dst_size, const char *src);
bool has_bad_path_chars(const char *path);
bool ends_with_ignore_case(const char *s, const char *suffix);
esp_err_t build_storage_path(const char *path, bool allow_root, char *out, size_t out_size);
esp_err_t get_query_value(httpd_req_t *req, const char *key, char *out, size_t out_size);
bool valid_upload_name(const char *name);
void json_escape_send(httpd_req_t *req, const char *text);
esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message);
size_t app_web_embedded_text_size(const uint8_t *start, const uint8_t *end);
esp_err_t send_embedded_html(httpd_req_t *req, const uint8_t *start, const uint8_t *end);
esp_err_t redirect_to_login(httpd_req_t *req);
esp_err_t read_form_body(httpd_req_t *req, char *body, size_t body_size);
esp_err_t get_form_value_decoded(const char *body, const char *key, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
