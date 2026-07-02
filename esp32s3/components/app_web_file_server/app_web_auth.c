#include "app_web_auth.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_web_common.h"
#include "esp_random.h"
#include "esp_timer.h"

#define WEB_AUTH_SESSION_BYTES 32
#define WEB_AUTH_SESSION_HEX_LEN (WEB_AUTH_SESSION_BYTES * 2)
#define WEB_AUTH_SESSION_TIMEOUT_US (30LL * 60LL * 1000000LL)

typedef struct {
    const char *username;
    const char *password;
    web_auth_role_t role;
    const char *role_name;
} web_auth_account_t;

static const web_auth_account_t s_web_auth_accounts[] = {
    {.username = "admin", .password = "admin", .role = WEB_AUTH_ROLE_ADMIN, .role_name = "admin"},
    {.username = "user", .password = "123", .role = WEB_AUTH_ROLE_USER, .role_name = "user"},
};

static char s_session_id[WEB_AUTH_SESSION_HEX_LEN + 1];
static char s_session_user[32];
static web_auth_role_t s_session_role;
static bool s_session_valid;
static int64_t s_session_expire_us;

static void web_auth_refresh_expire(void)
{
    s_session_expire_us = esp_timer_get_time() + WEB_AUTH_SESSION_TIMEOUT_US;
}

static void web_auth_clear_session(void)
{
    memset(s_session_id, 0, sizeof(s_session_id));
    memset(s_session_user, 0, sizeof(s_session_user));
    s_session_role = WEB_AUTH_ROLE_NONE;
    s_session_valid = false;
    s_session_expire_us = 0;
}

static const web_auth_account_t *web_auth_find_account(const char *username, const char *password)
{
    for (size_t i = 0; i < sizeof(s_web_auth_accounts) / sizeof(s_web_auth_accounts[0]); i++) {
        const web_auth_account_t *account = &s_web_auth_accounts[i];
        if (strcmp(username, account->username) == 0 &&
            strcmp(password, account->password) == 0) {
            return account;
        }
    }
    return NULL;
}

static const char *web_auth_role_name(web_auth_role_t role)
{
    switch (role) {
    case WEB_AUTH_ROLE_ADMIN:
        return "admin";
    case WEB_AUTH_ROLE_USER:
        return "user";
    case WEB_AUTH_ROLE_NONE:
    default:
        return "none";
    }
}

static void web_auth_create_session(const web_auth_account_t *account)
{
    uint8_t random_bytes[WEB_AUTH_SESSION_BYTES];
    static const char hex[] = "0123456789abcdef";

    if (account == NULL) {
        web_auth_clear_session();
        return;
    }

    esp_fill_random(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        s_session_id[i * 2] = hex[random_bytes[i] >> 4];
        s_session_id[i * 2 + 1] = hex[random_bytes[i] & 0x0f];
    }
    s_session_id[WEB_AUTH_SESSION_HEX_LEN] = '\0';
    snprintf(s_session_user, sizeof(s_session_user), "%s", account->username);
    s_session_role = account->role;
    s_session_valid = true;
    web_auth_refresh_expire();
}

static bool cookie_has_sid(const char *cookie_header, const char *sid)
{
    const char *p = cookie_header;
    size_t sid_len = strlen(sid);

    while (p != NULL && *p != '\0') {
        while (*p == ' ' || *p == ';') {
            p++;
        }

        const char *end = strchr(p, ';');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        while (len > 0 && isspace((unsigned char)p[len - 1])) {
            len--;
        }

        const char key[] = "sid=";
        if (len == sizeof(key) - 1 + sid_len &&
            strncmp(p, key, sizeof(key) - 1) == 0 &&
            memcmp(p + sizeof(key) - 1, sid, sid_len) == 0) {
            return true;
        }

        p = end != NULL ? end + 1 : NULL;
    }

    return false;
}

bool web_auth_get_session(httpd_req_t *req, web_auth_role_t *role)
{
    if (!s_session_valid || s_session_id[0] == '\0') {
        return false;
    }
    if (esp_timer_get_time() > s_session_expire_us) {
        web_auth_clear_session();
        return false;
    }

    size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (cookie_len == 0) {
        return false;
    }

    char *cookie = malloc(cookie_len + 1);
    if (cookie == NULL) {
        return false;
    }

    bool ok = false;
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, cookie_len + 1) == ESP_OK) {
        ok = cookie_has_sid(cookie, s_session_id);
    }
    free(cookie);

    if (ok) {
        web_auth_refresh_expire();
        if (role != NULL) {
            *role = s_session_role;
        }
    }
    return ok;
}

bool web_auth_is_logged_in(httpd_req_t *req)
{
    return web_auth_get_session(req, NULL);
}

esp_err_t require_logged_in(httpd_req_t *req, web_auth_role_t *role)
{
    web_auth_role_t current_role = WEB_AUTH_ROLE_NONE;
    if (!web_auth_get_session(req, &current_role)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }
    if (role != NULL) {
        *role = current_role;
    }
    return ESP_OK;
}

esp_err_t require_admin(httpd_req_t *req)
{
    web_auth_role_t role = WEB_AUTH_ROLE_NONE;
    esp_err_t ret = require_logged_in(req, &role);
    if (ret != ESP_OK) {
        return ret;
    }
    if (role != WEB_AUTH_ROLE_ADMIN) {
        return send_json_error(req, "403 Forbidden", "当前账号无管理员权限");
    }
    return ESP_OK;
}

esp_err_t login_handler(httpd_req_t *req)
{
    char body[APP_WEB_MAX_PATH];
    char username[64];
    char password[96];

    if (read_form_body(req, body, sizeof(body)) != ESP_OK ||
        get_form_value_decoded(body, "username", username, sizeof(username)) != ESP_OK ||
        get_form_value_decoded(body, "password", password, sizeof(password)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "登录请求格式错误");
    }

    const web_auth_account_t *account = web_auth_find_account(username, password);
    if (account == NULL) {
        return send_json_error(req, "401 Unauthorized", "用户名或密码错误");
    }

    if (s_session_valid && esp_timer_get_time() > s_session_expire_us) {
        web_auth_clear_session();
    }

    if (s_session_valid && s_session_id[0] != '\0') {
        if (web_auth_is_logged_in(req)) {
            if (strcmp(s_session_user, account->username) != 0) {
                return send_json_error(req, "409 Conflict", "当前浏览器已有其他用户登录，请先退出当前会话");
            }
            char response[96];
            snprintf(response, sizeof(response), "{\"ok\":true,\"user\":\"%s\",\"role\":\"%s\"}",
                     s_session_user, web_auth_role_name(s_session_role));
            httpd_resp_set_type(req, "application/json; charset=utf-8");
            set_no_store(req);
            set_connection_close(req);
            return httpd_resp_sendstr(req, response);
        }
        return send_json_error(req, "409 Conflict", "已有用户登录，请先退出当前会话或等待会话过期");
    }

    web_auth_create_session(account);
    char cookie[128];
    snprintf(cookie, sizeof(cookie), "sid=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=1800", s_session_id);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    char response[96];
    snprintf(response, sizeof(response), "{\"ok\":true,\"user\":\"%s\",\"role\":\"%s\"}",
             s_session_user, web_auth_role_name(s_session_role));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_no_store(req);
    set_connection_close(req);
    return httpd_resp_sendstr(req, response);
}

esp_err_t logout_handler(httpd_req_t *req)
{
    if (!s_session_valid || web_auth_is_logged_in(req)) {
        web_auth_clear_session();
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_no_store(req);
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t auth_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_no_store(req);
    web_auth_role_t role = WEB_AUTH_ROLE_NONE;
    if (web_auth_get_session(req, &role)) {
        char response[112];
        snprintf(response, sizeof(response), "{\"ok\":true,\"login\":true,\"user\":\"%s\",\"role\":\"%s\"}",
                 s_session_user, web_auth_role_name(role));
        set_connection_close(req);
        return httpd_resp_sendstr(req, response);
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":false,\"login\":false}");
}
