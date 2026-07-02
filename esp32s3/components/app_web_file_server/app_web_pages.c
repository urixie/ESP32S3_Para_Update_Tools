#include "app_web_pages.h"

#include <stdio.h>
#include <string.h>

#include "app_web_auth.h"
#include "app_web_common.h"
#include "esp_app_desc.h"

extern const uint8_t login_html_start[] asm("_binary_login_html_start");
extern const uint8_t login_html_end[] asm("_binary_login_html_end");
extern const uint8_t app_html_start[] asm("_binary_app_html_start");
extern const uint8_t app_html_end[] asm("_binary_app_html_end");
extern const uint8_t user_app_html_start[] asm("_binary_user_app_html_start");
extern const uint8_t user_app_html_end[] asm("_binary_user_app_html_end");

esp_err_t index_handler(httpd_req_t *req)
{
    log_heap_state("访问登录页前");
    if (web_auth_is_logged_in(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/app");
        set_no_store(req);
        set_connection_close(req);
        return httpd_resp_send(req, NULL, 0);
    }
    return send_embedded_html(req, login_html_start, login_html_end);
}

esp_err_t app_handler(httpd_req_t *req)
{
    log_heap_state("访问主页面前");
    web_auth_role_t role = WEB_AUTH_ROLE_NONE;
    if (!web_auth_get_session(req, &role)) {
        return redirect_to_login(req);
    }
    if (role == WEB_AUTH_ROLE_USER) {
        return send_embedded_html(req, user_app_html_start, user_app_html_end);
    }
    if (role != WEB_AUTH_ROLE_ADMIN) {
        return send_json_error(req, "403 Forbidden", "当前账号无权限访问页面");
    }
    return send_embedded_html(req, app_html_start, app_html_end);
}

static int month_number_from_name(const char *month_name)
{
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    for (int i = 0; i < 12; i++) {
        if (strncmp(month_name, months[i], 3) == 0) {
            return i + 1;
        }
    }
    return 0;
}

static void format_app_build_time(char *out, size_t out_size)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc == NULL || out == NULL || out_size == 0) {
        return;
    }

    char month_name[4] = {0};
    int day = 0;
    int year = 0;
    if (sscanf(desc->date, "%3s %d %d", month_name, &day, &year) == 3) {
        int month = month_number_from_name(month_name);
        if (month > 0) {
            snprintf(out, out_size, "%04d-%02d-%02d %s", year, month, day, desc->time);
            return;
        }
    }

    snprintf(out, out_size, "%s %s", desc->date, desc->time);
}

esp_err_t app_info_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_logged_in(req, NULL);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    char build_time[40] = {0};
    format_app_build_time(build_time, sizeof(build_time));

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_no_store(req);
    set_connection_close(req);
    httpd_resp_sendstr_chunk(req, "{\"ok\":true,\"buildTime\":\"");
    json_escape_send(req, build_time);
    httpd_resp_sendstr_chunk(req, "\"}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    set_connection_close(req);
    return httpd_resp_send(req, NULL, 0);
}
