#include "app_web_file_server.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_param_bin.h"
#include "app_param_board.h"
#include "app_storage_lock.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#define APP_WEB_MAX_PATH 320
#define APP_WEB_IO_BUF_SIZE 2048
#define APP_WEB_UPLOAD_BUF_SIZE 4096
#define APP_WEB_PARAM_FORM_BUF_SIZE 1536
#define WEB_AUTH_USER "admin"
#define WEB_AUTH_PASS "admin"
#define WEB_AUTH_SESSION_BYTES 32
#define WEB_AUTH_SESSION_HEX_LEN (WEB_AUTH_SESSION_BYTES * 2)
#define WEB_AUTH_SESSION_TIMEOUT_US (30LL * 60LL * 1000000LL)

static const char *TAG = "web_param";
static httpd_handle_t s_server;
static char s_mount_point[64];
static char s_session_id[WEB_AUTH_SESSION_HEX_LEN + 1];
static bool s_session_valid;
static int64_t s_session_expire_us;

extern const uint8_t login_html_start[] asm("_binary_login_html_start");
extern const uint8_t login_html_end[] asm("_binary_login_html_end");
extern const uint8_t app_html_start[] asm("_binary_app_html_start");
extern const uint8_t app_html_end[] asm("_binary_app_html_end");

static void log_heap_state(const char *where)
{
    ESP_LOGI(TAG, "heap %s: free=%lu, min=%lu, internal=%u, psram=%u",
             where,
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void set_connection_close(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Connection", "close");
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static esp_err_t url_decode(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;
    for (size_t i = 0; src != NULL && src[i] != '\0'; i++) {
        if (out + 1 >= dst_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (src[i] == '%' && isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi < 0 || lo < 0) {
                return ESP_ERR_INVALID_ARG;
            }
            dst[out++] = (char)((hi << 4) | lo);
            i += 2;
        } else if (src[i] == '+') {
            dst[out++] = ' ';
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
    return ESP_OK;
}

static bool has_bad_path_chars(const char *path)
{
    return path == NULL || strstr(path, "..") != NULL || strchr(path, '\\') != NULL || strchr(path, ':') != NULL;
}

static bool ends_with_ignore_case(const char *s, const char *suffix)
{
    if (s == NULL || suffix == NULL) {
        return false;
    }
    size_t sl = strlen(s);
    size_t xl = strlen(suffix);
    return sl >= xl && strcasecmp(s + sl - xl, suffix) == 0;
}

static esp_err_t build_storage_path(const char *path, bool allow_root, char *out, size_t out_size)
{
    char decoded[APP_WEB_MAX_PATH];
    ESP_RETURN_ON_ERROR(url_decode(decoded, sizeof(decoded), path != NULL ? path : ""), TAG, "decode failed");

    while (decoded[0] == '/') {
        memmove(decoded, decoded + 1, strlen(decoded));
    }

    if (decoded[0] == '\0') {
        if (!allow_root) {
            return ESP_ERR_INVALID_ARG;
        }
        int len = snprintf(out, out_size, "%s", s_mount_point);
        return (len > 0 && (size_t)len < out_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }

    if (has_bad_path_chars(decoded)) {
        return ESP_ERR_INVALID_ARG;
    }

    int len = snprintf(out, out_size, "%s/%s", s_mount_point, decoded);
    if (len <= 0 || (size_t)len >= out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t get_query_value(httpd_req_t *req, const char *key, char *out, size_t out_size)
{
    char query[APP_WEB_MAX_PATH];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (httpd_query_key_value(query, key, out, out_size) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static bool valid_upload_name(const char *name)
{
    if (name == NULL || name[0] == '\0' || has_bad_path_chars(name)) {
        return false;
    }
    for (const char *p = name; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || *p == '/' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            return false;
        }
    }
    return true;
}

static void json_escape_send(httpd_req_t *req, const char *text)
{
    if (text == NULL) {
        return;
    }
    for (const char *p = text; *p != '\0'; p++) {
        char esc[7];
        switch (*p) {
        case '\\':
            httpd_resp_sendstr_chunk(req, "\\\\");
            break;
        case '"':
            httpd_resp_sendstr_chunk(req, "\\\"");
            break;
        case '\n':
            httpd_resp_sendstr_chunk(req, "\\n");
            break;
        case '\r':
            httpd_resp_sendstr_chunk(req, "\\r");
            break;
        case '\t':
            httpd_resp_sendstr_chunk(req, "\\t");
            break;
        default:
            if ((unsigned char)*p < 0x20) {
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
                httpd_resp_sendstr_chunk(req, esc);
            } else {
                httpd_resp_send_chunk(req, p, 1);
            }
            break;
        }
    }
}

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    httpd_resp_sendstr_chunk(req, "{\"ok\":false,\"error\":\"");
    json_escape_send(req, message);
    httpd_resp_sendstr_chunk(req, "\"}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t send_embedded_html(httpd_req_t *req, const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_send(req, (const char *)start, end - start);
}

static esp_err_t redirect_to_login(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    set_connection_close(req);
    return httpd_resp_send(req, NULL, 0);
}

static void web_auth_refresh_expire(void)
{
    s_session_expire_us = esp_timer_get_time() + WEB_AUTH_SESSION_TIMEOUT_US;
}

static void web_auth_clear_session(void)
{
    memset(s_session_id, 0, sizeof(s_session_id));
    s_session_valid = false;
    s_session_expire_us = 0;
}

static void web_auth_create_session(void)
{
    uint8_t random_bytes[WEB_AUTH_SESSION_BYTES];
    static const char hex[] = "0123456789abcdef";

    esp_fill_random(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        s_session_id[i * 2] = hex[random_bytes[i] >> 4];
        s_session_id[i * 2 + 1] = hex[random_bytes[i] & 0x0f];
    }
    s_session_id[WEB_AUTH_SESSION_HEX_LEN] = '\0';
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

static bool web_auth_is_logged_in(httpd_req_t *req)
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
    }
    return ok;
}

static esp_err_t read_form_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (req->content_len <= 0 || req->content_len >= body_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    int remaining = req->content_len;
    int offset = 0;
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, body + offset, remaining);
        if (recv_len <= 0) {
            return ESP_FAIL;
        }
        remaining -= recv_len;
        offset += recv_len;
    }
    body[offset] = '\0';
    return ESP_OK;
}

static esp_err_t get_form_value_decoded(const char *body, const char *key, char *out, size_t out_size)
{
    char encoded[APP_WEB_MAX_PATH];
    if (httpd_query_key_value(body, key, encoded, sizeof(encoded)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    return url_decode(out, out_size, encoded);
}

static esp_err_t index_handler(httpd_req_t *req)
{
    log_heap_state("before index");
    if (web_auth_is_logged_in(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/app");
        set_connection_close(req);
        return httpd_resp_send(req, NULL, 0);
    }
    return send_embedded_html(req, login_html_start, login_html_end);
}

static esp_err_t app_handler(httpd_req_t *req)
{
    log_heap_state("before app");
    if (!web_auth_is_logged_in(req)) {
        return redirect_to_login(req);
    }
    return send_embedded_html(req, app_html_start, app_html_end);
}

static esp_err_t login_handler(httpd_req_t *req)
{
    char body[APP_WEB_MAX_PATH];
    char username[64];
    char password[96];

    if (read_form_body(req, body, sizeof(body)) != ESP_OK ||
        get_form_value_decoded(body, "username", username, sizeof(username)) != ESP_OK ||
        get_form_value_decoded(body, "password", password, sizeof(password)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "登录请求格式错误");
    }

    if (strcmp(username, WEB_AUTH_USER) != 0 || strcmp(password, WEB_AUTH_PASS) != 0) {
        return send_json_error(req, "401 Unauthorized", "用户名或密码错误");
    }

    if (s_session_valid && esp_timer_get_time() > s_session_expire_us) {
        web_auth_clear_session();
    }

    if (s_session_valid && s_session_id[0] != '\0') {
        if (web_auth_is_logged_in(req)) {
            httpd_resp_set_type(req, "application/json; charset=utf-8");
            set_connection_close(req);
            return httpd_resp_sendstr(req, "{\"ok\":true}");
        }
        return send_json_error(req, "409 Conflict", "已有用户登录，请先退出当前会话或等待会话过期");
    }

    web_auth_create_session();
    char cookie[128];
    snprintf(cookie, sizeof(cookie), "sid=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=1800", s_session_id);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t logout_handler(httpd_req_t *req)
{
    if (!s_session_valid || web_auth_is_logged_in(req)) {
        web_auth_clear_session();
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t auth_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    if (web_auth_is_logged_in(req)) {
        set_connection_close(req);
        return httpd_resp_sendstr(req, "{\"ok\":true,\"login\":true,\"user\":\"admin\"}");
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":false,\"login\":false}");
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    set_connection_close(req);
    return httpd_resp_send(req, NULL, 0);
}

static const char *content_type_from_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext != NULL) {
        if (strcasecmp(ext, ".png") == 0) {
            return "image/png";
        }
        if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
            return "image/jpeg";
        }
        if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
            return "text/html; charset=utf-8";
        }
        if (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".log") == 0) {
            return "text/plain; charset=utf-8";
        }
    }
    return "application/octet-stream";
}

static const char *kind_label_from_name(const char *name, bool is_dir)
{
    if (is_dir) {
        return "目录";
    }
    if (ends_with_ignore_case(name, ".bin")) {
        return "加密板卡 bin";
    }
    return "普通文件";
}

static bool try_parse_board_name(const char *relative_path, char *board_name, size_t board_name_size)
{
    if (board_name == NULL || board_name_size == 0) {
        return false;
    }
    board_name[0] = '\0';

    char full_path[APP_WEB_MAX_PATH];
    int len = snprintf(full_path, sizeof(full_path), "%s%s", s_mount_point, relative_path);
    if (len <= 0 || (size_t)len >= sizeof(full_path)) {
        return false;
    }

    app_param_bin_result_t *parsed = calloc(1, sizeof(*parsed));
    if (parsed == NULL) {
        return false;
    }

    char parse_error[96] = {0};
    esp_err_t ret = app_param_bin_parse_file(full_path, parsed, parse_error, sizeof(parse_error));
    if (ret == ESP_OK && parsed->board_name[0] != '\0') {
        snprintf(board_name, board_name_size, "%s", parsed->board_name);
        free(parsed);
        return true;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "skip board name for %s: %s", full_path, parse_error);
    }
    free(parsed);
    return false;
}

static esp_err_t send_file_item(httpd_req_t *req, const char *relative_path, const char *name,
                                const struct stat *st, bool *first)
{
    bool is_dir = S_ISDIR(st->st_mode);
    bool is_param_bin = !is_dir && ends_with_ignore_case(name, ".bin");
    char board_name[APP_PARAM_BIN_BOARD_NAME_MAX_BYTES + 1] = {0};
    const char *display_name = name;

    if (is_param_bin && try_parse_board_name(relative_path, board_name, sizeof(board_name))) {
        display_name = board_name;
    }

    char meta[256];
    snprintf(meta, sizeof(meta), "%s{\"name\":\"", *first ? "" : ",");
    httpd_resp_sendstr_chunk(req, meta);
    json_escape_send(req, name);
    httpd_resp_sendstr_chunk(req, "\",\"path\":\"");
    json_escape_send(req, relative_path);
    httpd_resp_sendstr_chunk(req, "\",\"boardName\":\"");
    json_escape_send(req, is_param_bin ? board_name : "");
    httpd_resp_sendstr_chunk(req, "\",\"displayName\":\"");
    json_escape_send(req, display_name);
    snprintf(meta, sizeof(meta),
             "\",\"size\":%lld,\"is_dir\":%s,\"is_param_bin\":%s,\"kind\":\"%s\",\"kind_label\":\"%s\"}",
             (long long)st->st_size,
             is_dir ? "true" : "false",
             is_param_bin ? "true" : "false",
             is_param_bin ? "param_bin" : (is_dir ? "dir" : "file"),
             kind_label_from_name(name, is_dir));
    httpd_resp_sendstr_chunk(req, meta);
    *first = false;
    return ESP_OK;
}

static esp_err_t list_dir_entries(httpd_req_t *req, const char *relative_dir, bool *first)
{
    char *full_dir = malloc(APP_WEB_MAX_PATH);
    char *relative_path = malloc(APP_WEB_MAX_PATH);
    char *full_path = malloc(APP_WEB_MAX_PATH);
    if (full_dir == NULL || relative_path == NULL || full_path == NULL) {
        free(full_dir);
        free(relative_path);
        free(full_path);
        return ESP_ERR_NO_MEM;
    }

    int len = relative_dir[0] == '\0'
                  ? snprintf(full_dir, APP_WEB_MAX_PATH, "%s", s_mount_point)
                  : snprintf(full_dir, APP_WEB_MAX_PATH, "%s/%s", s_mount_point, relative_dir);
    if (len <= 0 || (size_t)len >= APP_WEB_MAX_PATH) {
        free(full_dir);
        free(relative_path);
        free(full_path);
        return ESP_ERR_INVALID_SIZE;
    }

    DIR *dir = opendir(full_dir);
    if (dir == NULL) {
        free(full_dir);
        free(relative_path);
        free(full_path);
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, "System Volume Information") == 0 ||
            has_bad_path_chars(entry->d_name)) {
            continue;
        }

        int rel_len = relative_dir[0] == '\0'
                          ? snprintf(relative_path, APP_WEB_MAX_PATH, "/%s", entry->d_name)
                          : snprintf(relative_path, APP_WEB_MAX_PATH, "/%s/%s", relative_dir, entry->d_name);
        if (rel_len <= 0 || (size_t)rel_len >= APP_WEB_MAX_PATH) {
            continue;
        }

        int full_len = snprintf(full_path, APP_WEB_MAX_PATH, "%s%s", s_mount_point, relative_path);
        if (full_len <= 0 || (size_t)full_len >= APP_WEB_MAX_PATH) {
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        send_file_item(req, relative_path, entry->d_name, &st, first);
    }

    closedir(dir);
    free(full_dir);
    free(relative_path);
    free(full_path);
    return ESP_OK;
}

static esp_err_t files_handler(httpd_req_t *req)
{
    log_heap_state("before files");
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);

    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }

    DIR *dir = opendir(s_mount_point);
    if (dir == NULL) {
        int err = errno;
        app_storage_unlock();
        ESP_LOGE(TAG, "opendir failed: errno=%d", err);
        return send_json_error(req, "500 Internal Server Error", "无法打开存储目录");
    }
    closedir(dir);

    httpd_resp_sendstr_chunk(req, "{\"mount_point\":\"");
    json_escape_send(req, s_mount_point);
    httpd_resp_sendstr_chunk(req, "\",\"files\":[");
    bool first = true;
    list_dir_entries(req, "", &first);
    app_storage_unlock();
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t stream_file(httpd_req_t *req, const char *full_path, bool attachment)
{
    FILE *f = fopen(full_path, "rb");
    if (f == NULL) {
        return send_json_error(req, "404 Not Found", "文件不存在");
    }

    httpd_resp_set_type(req, content_type_from_path(full_path));
    set_connection_close(req);
    if (attachment) {
        char disposition[APP_WEB_MAX_PATH + 64];
        const char *name = strrchr(full_path, '/');
        snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", name != NULL ? name + 1 : "download.bin");
        httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    }

    char *buf = malloc(APP_WEB_IO_BUF_SIZE);
    if (buf == NULL) {
        fclose(f);
        return send_json_error(req, "500 Internal Server Error", "内存不足");
    }

    esp_err_t ret = ESP_OK;
    size_t read_len;
    while ((read_len = fread(buf, 1, APP_WEB_IO_BUF_SIZE, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_len) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
    }

    free(buf);
    fclose(f);

    if (ret != ESP_OK) {
        return ret;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t download_handler(httpd_req_t *req)
{
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    char raw_path[APP_WEB_MAX_PATH];
    char full_path[APP_WEB_MAX_PATH];
    if (get_query_value(req, "path", raw_path, sizeof(raw_path)) != ESP_OK ||
        build_storage_path(raw_path, false, full_path, sizeof(full_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "路径无效");
    }

    ESP_LOGI(TAG, "download start: %s", full_path);
    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }
    esp_err_t ret = stream_file(req, full_path, true);
    app_storage_unlock();
    ESP_LOGI(TAG, "download done: %s", full_path);
    return ret;
}

static esp_err_t upload_handler(httpd_req_t *req)
{
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    if (req->content_len <= 0) {
        return send_json_error(req, "400 Bad Request", "上传内容为空");
    }

    char filename_encoded[APP_WEB_MAX_PATH];
    char filename[APP_WEB_MAX_PATH];
    if (get_query_value(req, "filename", filename_encoded, sizeof(filename_encoded)) != ESP_OK ||
        url_decode(filename, sizeof(filename), filename_encoded) != ESP_OK ||
        !valid_upload_name(filename)) {
        return send_json_error(req, "400 Bad Request", "文件名无效");
    }

    char full_path[APP_WEB_MAX_PATH];
    int len = snprintf(full_path, sizeof(full_path), "%s/%s", s_mount_point, filename);
    if (len <= 0 || (size_t)len >= sizeof(full_path)) {
        return send_json_error(req, "400 Bad Request", "文件名过长");
    }

    char tmp_path[APP_WEB_MAX_PATH];
    len = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", full_path);
    if (len <= 0 || (size_t)len >= sizeof(tmp_path)) {
        return send_json_error(req, "400 Bad Request", "文件名过长");
    }

    log_heap_state("upload start");
    ESP_LOGI(TAG, "upload start: %s, tmp=%s, size=%zu", full_path, tmp_path, req->content_len);
    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }

    FILE *f = fopen(tmp_path, "wb");
    if (f == NULL) {
        int err = errno;
        app_storage_unlock();
        ESP_LOGE(TAG, "open tmp upload file failed: %s, errno=%d", tmp_path, err);
        return send_json_error(req, "500 Internal Server Error", "创建文件失败");
    }

    char *buf = malloc(APP_WEB_UPLOAD_BUF_SIZE);
    if (buf == NULL) {
        fclose(f);
        remove(tmp_path);
        app_storage_unlock();
        return send_json_error(req, "500 Internal Server Error", "内存不足");
    }

    const size_t total = req->content_len;
    size_t remaining = total;
    size_t received = 0;
    size_t last_log = 0;
    int timeout_count = 0;
    const int max_timeout_count = 30;
    esp_err_t ret = ESP_OK;

    while (remaining > 0) {
        size_t want = remaining > APP_WEB_UPLOAD_BUF_SIZE ? APP_WEB_UPLOAD_BUF_SIZE : remaining;
        int recv_len = httpd_req_recv(req, buf, want);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT && timeout_count++ < max_timeout_count) {
                ESP_LOGW(TAG, "upload recv timeout %d/%d, received=%zu/%zu",
                         timeout_count, max_timeout_count, received, total);
                continue;
            }
            ESP_LOGE(TAG, "upload recv failed: recv_len=%d, received=%zu/%zu", recv_len, received, total);
            ret = recv_len == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
            break;
        }

        timeout_count = 0;
        if (fwrite(buf, 1, recv_len, f) != (size_t)recv_len) {
            int err = errno;
            ESP_LOGE(TAG, "upload write failed, received=%zu/%zu, errno=%d", received, total, err);
            ret = ESP_FAIL;
            break;
        }

        remaining -= recv_len;
        received += recv_len;
        if (received == total || received - last_log >= 64 * 1024) {
            ESP_LOGI(TAG, "upload progress: %zu/%zu", received, total);
            last_log = received;
        }
    }

    free(buf);

    if (ret == ESP_OK && received != total) {
        ESP_LOGE(TAG, "upload size mismatch: received=%zu/%zu", received, total);
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK && fflush(f) != 0) {
        int err = errno;
        ESP_LOGE(TAG, "upload fflush failed errno=%d", err);
        ret = ESP_FAIL;
    }

    if (fclose(f) != 0 && ret == ESP_OK) {
        int err = errno;
        ESP_LOGE(TAG, "upload fclose failed errno=%d", err);
        ret = ESP_FAIL;
    }
    f = NULL;

    if (ret == ESP_OK) {
        remove(full_path);
        if (rename(tmp_path, full_path) != 0) {
            int err = errno;
            ESP_LOGE(TAG, "upload rename failed: %s -> %s, errno=%d", tmp_path, full_path, err);
            ret = ESP_FAIL;
        }
    }

    if (ret != ESP_OK) {
        remove(tmp_path);
    }

    app_storage_unlock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "upload failed: %s, received=%zu/%zu, err=%s",
                 full_path, received, total, esp_err_to_name(ret));
        return send_json_error(req, "500 Internal Server Error", "上传失败");
    }

    log_heap_state("upload done");
    ESP_LOGI(TAG, "upload done: %s, received=%zu", full_path, received);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t delete_handler(httpd_req_t *req)
{
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    char body[APP_WEB_MAX_PATH];
    if (read_form_body(req, body, sizeof(body)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少请求内容");
    }

    char encoded_path[APP_WEB_MAX_PATH] = {0};
    if (httpd_query_key_value(body, "path", encoded_path, sizeof(encoded_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少文件路径");
    }

    char full_path[APP_WEB_MAX_PATH];
    if (build_storage_path(encoded_path, false, full_path, sizeof(full_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "路径无效");
    }

    ESP_LOGI(TAG, "delete start: %s", full_path);
    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }

    struct stat st;
    if (stat(full_path, &st) != 0) {
        app_storage_unlock();
        return send_json_error(req, "404 Not Found", "文件不存在");
    }
    if (S_ISDIR(st.st_mode)) {
        app_storage_unlock();
        return send_json_error(req, "400 Bad Request", "暂不支持删除目录");
    }
    if (remove(full_path) != 0) {
        int err = errno;
        app_storage_unlock();
        ESP_LOGE(TAG, "delete failed errno=%d", err);
        return send_json_error(req, "500 Internal Server Error", "删除失败");
    }

    app_storage_unlock();
    ESP_LOGI(TAG, "delete done: %s", full_path);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static void send_visible_param_json(httpd_req_t *req, const app_param_bin_parameter_t *p, bool first)
{
    char buf[192];
    snprintf(buf, sizeof(buf), "%s{\"name\":\"", first ? "" : ",");
    httpd_resp_sendstr_chunk(req, buf);
    json_escape_send(req, p->name);
    snprintf(buf, sizeof(buf),
             "\",\"address\":%u,\"defaultValue\":%u,\"paramType\":\"%s\",\"paramTypeLabel\":\"%s\"}",
             p->address,
             p->default_value,
             app_param_bin_type_name(p->param_type),
             app_param_bin_type_label(p->param_type));
    httpd_resp_sendstr_chunk(req, buf);
}

static void build_param_defaults(const app_param_bin_result_t *parsed, uint16_t defaults[APP_PARAM_BIN_PARAM_COUNT])
{
    memset(defaults, 0, sizeof(uint16_t) * APP_PARAM_BIN_PARAM_COUNT);
    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const app_param_bin_parameter_t *p = &parsed->parameters[i];
        if (p->address < APP_PARAM_BIN_PARAM_COUNT) {
            defaults[p->address] = p->default_value;
        }
    }
}

static bool param_address_is_visible(const app_param_bin_result_t *parsed, uint8_t address)
{
    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const app_param_bin_parameter_t *p = &parsed->parameters[i];
        if (p->address == address) {
            return p->permission == APP_PARAM_BIN_PERMISSION_VISIBLE;
        }
    }
    return false;
}

static esp_err_t parse_param_values(const char *text,
                                    const app_param_bin_result_t *parsed,
                                    bool provided[APP_PARAM_BIN_PARAM_COUNT],
                                    uint16_t values[APP_PARAM_BIN_PARAM_COUNT],
                                    char *err_msg,
                                    size_t err_msg_size)
{
    if (text == NULL || text[0] == '\0') {
        snprintf(err_msg, err_msg_size, "缺少参数值");
        return ESP_ERR_INVALID_ARG;
    }

    char buf[1024];
    int len = snprintf(buf, sizeof(buf), "%s", text);
    if (len <= 0 || (size_t)len >= sizeof(buf)) {
        snprintf(err_msg, err_msg_size, "参数值过长");
        return ESP_ERR_INVALID_SIZE;
    }

    char *saveptr = NULL;
    char *token = strtok_r(buf, ",", &saveptr);
    while (token != NULL) {
        char *sep = strchr(token, ':');
        if (sep == NULL) {
            snprintf(err_msg, err_msg_size, "参数格式错误");
            return ESP_ERR_INVALID_ARG;
        }
        *sep = '\0';
        char *end = NULL;
        unsigned long addr = strtoul(token, &end, 10);
        if (end == token || *end != '\0' || addr >= APP_PARAM_BIN_PARAM_COUNT) {
            snprintf(err_msg, err_msg_size, "参数地址无效");
            return ESP_ERR_INVALID_ARG;
        }
        unsigned long value = strtoul(sep + 1, &end, 10);
        if (end == sep + 1 || *end != '\0' || value > 65535UL) {
            snprintf(err_msg, err_msg_size, "参数值无效");
            return ESP_ERR_INVALID_ARG;
        }
        if (!param_address_is_visible(parsed, (uint8_t)addr)) {
            snprintf(err_msg, err_msg_size, "参数地址未授权");
            return ESP_ERR_INVALID_ARG;
        }
        provided[addr] = true;
        values[addr] = (uint16_t)value;
        token = strtok_r(NULL, ",", &saveptr);
    }

    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const app_param_bin_parameter_t *p = &parsed->parameters[i];
        if (p->permission == APP_PARAM_BIN_PERMISSION_VISIBLE && !provided[p->address]) {
            snprintf(err_msg, err_msg_size, "存在未填写的可见参数");
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

static esp_err_t bin_parse_handler(httpd_req_t *req)
{
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    char raw_path[APP_WEB_MAX_PATH];
    char full_path[APP_WEB_MAX_PATH];
    if (get_query_value(req, "path", raw_path, sizeof(raw_path)) != ESP_OK ||
        build_storage_path(raw_path, false, full_path, sizeof(full_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "路径无效");
    }

    if (!ends_with_ignore_case(full_path, ".bin")) {
        return send_json_error(req, "400 Bad Request", "只支持解析 .bin 文件");
    }

    app_param_bin_result_t *parsed = calloc(1, sizeof(*parsed));
    if (parsed == NULL) {
        return send_json_error(req, "500 Internal Server Error", "内存不足");
    }

    char parse_error[160];
    parse_error[0] = '\0';

    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        free(parsed);
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }
    esp_err_t ret = app_param_bin_parse_file(full_path, parsed, parse_error, sizeof(parse_error));
    app_storage_unlock();

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "parse failed %s: %s", full_path, parse_error);
        free(parsed);
        return send_json_error(req, "422 Unprocessable Entity", parse_error[0] != '\0' ? parse_error : "解析失败");
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    httpd_resp_sendstr_chunk(req, "{\"ok\":true,\"boardName\":\"");
    json_escape_send(req, parsed->board_name);
    httpd_resp_sendstr_chunk(req, "\",\"parameters\":[");

    bool first = true;
    size_t visible_count = 0;
    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const app_param_bin_parameter_t *param = &parsed->parameters[i];
        if (param->permission != APP_PARAM_BIN_PERMISSION_VISIBLE) {
            continue;
        }
        send_visible_param_json(req, param, first);
        first = false;
        visible_count++;
    }

    char tail[64];
    snprintf(tail, sizeof(tail), "],\"visibleCount\":%u}", (unsigned int)visible_count);
    httpd_resp_sendstr_chunk(req, tail);
    free(parsed);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t load_parsed_bin(const char *encoded_path,
                                 app_param_bin_result_t **out,
                                 char *err_msg,
                                 size_t err_msg_size)
{
    char full_path[APP_WEB_MAX_PATH];
    if (build_storage_path(encoded_path, false, full_path, sizeof(full_path)) != ESP_OK) {
        snprintf(err_msg, err_msg_size, "路径无效");
        return ESP_ERR_INVALID_ARG;
    }
    if (!ends_with_ignore_case(full_path, ".bin")) {
        snprintf(err_msg, err_msg_size, "只支持 .bin 板卡配置");
        return ESP_ERR_INVALID_ARG;
    }

    app_param_bin_result_t *parsed = calloc(1, sizeof(*parsed));
    if (parsed == NULL) {
        snprintf(err_msg, err_msg_size, "内存不足");
        return ESP_ERR_NO_MEM;
    }

    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        free(parsed);
        snprintf(err_msg, err_msg_size, "文件系统忙");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = app_param_bin_parse_file(full_path, parsed, err_msg, err_msg_size);
    app_storage_unlock();
    if (ret != ESP_OK) {
        free(parsed);
        return ret;
    }

    *out = parsed;
    return ESP_OK;
}

static void send_visible_values_json(httpd_req_t *req,
                                     const app_param_bin_result_t *parsed,
                                     const uint16_t values[APP_PARAM_BIN_PARAM_COUNT])
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    httpd_resp_sendstr_chunk(req, "{\"ok\":true,\"values\":[");
    bool first = true;
    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const app_param_bin_parameter_t *p = &parsed->parameters[i];
        if (p->permission != APP_PARAM_BIN_PERMISSION_VISIBLE) {
            continue;
        }
        char item[64];
        snprintf(item, sizeof(item), "%s{\"address\":%u,\"value\":%u}",
                 first ? "" : ",", p->address, values[p->address]);
        httpd_resp_sendstr_chunk(req, item);
        first = false;
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t param_readback_handler(httpd_req_t *req)
{
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    char body[APP_WEB_PARAM_FORM_BUF_SIZE];
    if (read_form_body(req, body, sizeof(body)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少请求内容");
    }

    char encoded_path[APP_WEB_MAX_PATH] = {0};
    if (httpd_query_key_value(body, "path", encoded_path, sizeof(encoded_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少文件路径");
    }

    char err_msg[160] = {0};
    app_param_bin_result_t *parsed = NULL;
    esp_err_t ret = load_parsed_bin(encoded_path, &parsed, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        return send_json_error(req, ret == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "422 Unprocessable Entity",
                               err_msg[0] ? err_msg : "解析板卡配置失败");
    }

    uint16_t defaults[APP_PARAM_BIN_PARAM_COUNT];
    uint16_t values[APP_PARAM_BIN_PARAM_COUNT];
    build_param_defaults(parsed, defaults);
    ret = app_param_board_read(defaults, values, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        free(parsed);
        return send_json_error(req, "500 Internal Server Error", err_msg[0] ? err_msg : "参数回读失败");
    }

    send_visible_values_json(req, parsed, values);
    free(parsed);
    return ESP_OK;
}

static esp_err_t param_download_handler(httpd_req_t *req)
{
    if (!web_auth_is_logged_in(req)) {
        return send_json_error(req, "401 Unauthorized", "请先登录");
    }

    char body[APP_WEB_PARAM_FORM_BUF_SIZE];
    if (read_form_body(req, body, sizeof(body)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少请求内容");
    }

    char encoded_path[APP_WEB_MAX_PATH] = {0};
    if (httpd_query_key_value(body, "path", encoded_path, sizeof(encoded_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少文件路径");
    }

    char values_text[1024] = {0};
    if (get_form_value_decoded(body, "values", values_text, sizeof(values_text)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少参数值");
    }

    char err_msg[160] = {0};
    app_param_bin_result_t *parsed = NULL;
    esp_err_t ret = load_parsed_bin(encoded_path, &parsed, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        return send_json_error(req, ret == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "422 Unprocessable Entity",
                               err_msg[0] ? err_msg : "解析板卡配置失败");
    }

    bool provided[APP_PARAM_BIN_PARAM_COUNT] = {0};
    uint16_t submitted[APP_PARAM_BIN_PARAM_COUNT] = {0};
    ret = parse_param_values(values_text, parsed, provided, submitted, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        free(parsed);
        return send_json_error(req, "400 Bad Request", err_msg[0] ? err_msg : "参数值无效");
    }

    uint16_t values[APP_PARAM_BIN_PARAM_COUNT];
    /*
     * 参数下载以加密 bin 中的 72 项默认值作为基础镜像：
     * - 隐藏参数不会从网页提交，保持加密 bin 默认值；
     * - 可见参数通过网页提交值覆盖对应 address；
     * - 不再通过回读值保留隐藏参数，避免隐藏参数被旧板卡状态污染。
     */
    build_param_defaults(parsed, values);

    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        if (provided[i]) {
            values[i] = submitted[i];
        }
    }

    ret = app_param_board_write(values, err_msg, sizeof(err_msg));
    free(parsed);
    if (ret != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", err_msg[0] ? err_msg : "参数下载失败");
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t app_web_file_server_start(const char *mount_point)
{
    ESP_RETURN_ON_FALSE(mount_point != NULL && mount_point[0] == '/', ESP_ERR_INVALID_ARG,
                        TAG, "invalid mount point");

    if (s_server != NULL) {
        return ESP_OK;
    }

    int len = snprintf(s_mount_point, sizeof(s_mount_point), "%s", mount_point);
    ESP_RETURN_ON_FALSE(len > 0 && (size_t)len < sizeof(s_mount_point), ESP_ERR_INVALID_SIZE,
                        TAG, "mount point too long");

    log_heap_state("before server start");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 12288;
    config.max_uri_handlers = 14;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 60;
    config.send_wait_timeout = 30;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    config.keep_alive_enable = false;
#endif

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd_start failed");
    log_heap_state("after server start");

    const httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    const httpd_uri_t app_uri = {.uri = "/app", .method = HTTP_GET, .handler = app_handler};
    const httpd_uri_t login_uri = {.uri = "/api/login", .method = HTTP_POST, .handler = login_handler};
    const httpd_uri_t logout_uri = {.uri = "/api/logout", .method = HTTP_POST, .handler = logout_handler};
    const httpd_uri_t auth_status_uri = {.uri = "/api/auth/status", .method = HTTP_GET, .handler = auth_status_handler};
    const httpd_uri_t favicon_uri = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler};
    const httpd_uri_t files_uri = {.uri = "/files", .method = HTTP_GET, .handler = files_handler};
    const httpd_uri_t download_uri = {.uri = "/download", .method = HTTP_GET, .handler = download_handler};
    const httpd_uri_t upload_uri = {.uri = "/upload", .method = HTTP_POST, .handler = upload_handler};
    const httpd_uri_t delete_uri = {.uri = "/delete", .method = HTTP_POST, .handler = delete_handler};
    const httpd_uri_t bin_parse_uri = {.uri = "/api/bin/parse", .method = HTTP_GET, .handler = bin_parse_handler};
    const httpd_uri_t param_readback_uri = {.uri = "/api/param/readback", .method = HTTP_POST, .handler = param_readback_handler};
    const httpd_uri_t param_download_uri = {.uri = "/api/param/download", .method = HTTP_POST, .handler = param_download_handler};

    esp_err_t ret = httpd_register_uri_handler(s_server, &index_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register / failed");
    ret = httpd_register_uri_handler(s_server, &app_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /app failed");
    ret = httpd_register_uri_handler(s_server, &login_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /api/login failed");
    ret = httpd_register_uri_handler(s_server, &logout_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /api/logout failed");
    ret = httpd_register_uri_handler(s_server, &auth_status_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /api/auth/status failed");
    ret = httpd_register_uri_handler(s_server, &favicon_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /favicon.ico failed");
    ret = httpd_register_uri_handler(s_server, &files_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /files failed");
    ret = httpd_register_uri_handler(s_server, &download_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /download failed");
    ret = httpd_register_uri_handler(s_server, &upload_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /upload failed");
    ret = httpd_register_uri_handler(s_server, &delete_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /delete failed");
    ret = httpd_register_uri_handler(s_server, &bin_parse_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /api/bin/parse failed");
    ret = httpd_register_uri_handler(s_server, &param_readback_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /api/param/readback failed");
    ret = httpd_register_uri_handler(s_server, &param_download_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, TAG, "register /api/param/download failed");

    ESP_LOGI(TAG, "HTTP parameter bin server started at mount point %s", s_mount_point);
    return ESP_OK;

err_stop:
    httpd_stop(s_server);
    s_server = NULL;
    return ret;
}
