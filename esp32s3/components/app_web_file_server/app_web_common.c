#include "app_web_common.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static char s_mount_point[64];

esp_err_t app_web_set_mount_point(const char *mount_point)
{
    ESP_RETURN_ON_FALSE(mount_point != NULL && mount_point[0] == '/', ESP_ERR_INVALID_ARG,
                        APP_WEB_TAG, "invalid mount point");

    int len = snprintf(s_mount_point, sizeof(s_mount_point), "%s", mount_point);
    ESP_RETURN_ON_FALSE(len > 0 && (size_t)len < sizeof(s_mount_point), ESP_ERR_INVALID_SIZE,
                        APP_WEB_TAG, "mount point too long");
    return ESP_OK;
}

const char *app_web_mount_point(void)
{
    return s_mount_point;
}

void log_heap_state(const char *where)
{
    size_t total_size = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t free_size = esp_get_free_heap_size();
    size_t min_free_size = esp_get_minimum_free_heap_size();
    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    unsigned int total_used_x10 = total_size > 0 ? (unsigned int)(((uint64_t)(total_size - free_size) * 1000U) / total_size) : 0;
    unsigned int internal_used_x10 = internal_total > 0 ? (unsigned int)(((uint64_t)(internal_total - internal_free) * 1000U) / internal_total) : 0;
    unsigned int psram_used_x10 = psram_total > 0 ? (unsigned int)(((uint64_t)(psram_total - psram_free) * 1000U) / psram_total) : 0;
    unsigned long total_kb = (unsigned long)((total_size + 512U) / 1024U);
    unsigned long free_kb = (unsigned long)((free_size + 512U) / 1024U);
    unsigned long min_free_kb = (unsigned long)((min_free_size + 512U) / 1024U);
    unsigned long internal_total_kb = (unsigned long)((internal_total + 512U) / 1024U);
    unsigned long internal_free_kb = (unsigned long)((internal_free + 512U) / 1024U);
    unsigned long psram_total_kb = (unsigned long)((psram_total + 512U) / 1024U);
    unsigned long psram_free_kb = (unsigned long)((psram_free + 512U) / 1024U);

    ESP_LOGI(APP_WEB_TAG, "内存状态[%s]", where);
    ESP_LOGI(APP_WEB_TAG,
             "  总内存: 已用 %u.%u%%, 剩余 %lu/%lu KB, 历史最低剩余 %lu KB",
             total_used_x10 / 10,
             total_used_x10 % 10,
             free_kb,
             total_kb,
             min_free_kb);
    ESP_LOGI(APP_WEB_TAG,
             "  内部RAM: 已用 %u.%u%%, 剩余 %lu/%lu KB",
             internal_used_x10 / 10,
             internal_used_x10 % 10,
             internal_free_kb,
             internal_total_kb);
    ESP_LOGI(APP_WEB_TAG,
             "  PSRAM: 已用 %u.%u%%, 剩余 %lu/%lu KB",
             psram_used_x10 / 10,
             psram_used_x10 % 10,
             psram_free_kb,
             psram_total_kb);
}

void set_connection_close(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Connection", "close");
}

void set_no_store(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
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

esp_err_t url_decode(char *dst, size_t dst_size, const char *src)
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

bool has_bad_path_chars(const char *path)
{
    return path == NULL || strstr(path, "..") != NULL || strchr(path, '\\') != NULL || strchr(path, ':') != NULL;
}

bool ends_with_ignore_case(const char *s, const char *suffix)
{
    if (s == NULL || suffix == NULL) {
        return false;
    }
    size_t sl = strlen(s);
    size_t xl = strlen(suffix);
    return sl >= xl && strcasecmp(s + sl - xl, suffix) == 0;
}

esp_err_t build_storage_path(const char *path, bool allow_root, char *out, size_t out_size)
{
    char decoded[APP_WEB_MAX_PATH];
    ESP_RETURN_ON_ERROR(url_decode(decoded, sizeof(decoded), path != NULL ? path : ""), APP_WEB_TAG, "decode failed");

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

esp_err_t get_query_value(httpd_req_t *req, const char *key, char *out, size_t out_size)
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

bool valid_upload_name(const char *name)
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

void json_escape_send(httpd_req_t *req, const char *text)
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

esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_no_store(req);
    set_connection_close(req);
    httpd_resp_sendstr_chunk(req, "{\"ok\":false,\"error\":\"");
    json_escape_send(req, message);
    httpd_resp_sendstr_chunk(req, "\"}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

size_t app_web_embedded_text_size(const uint8_t *start, const uint8_t *end)
{
    if (start == NULL || end == NULL || end <= start) {
        return 0;
    }

    size_t size = (size_t)(end - start);
    if (size > 0 && start[size - 1] == '\0') {
        size--;
    }
    return size;
}

esp_err_t send_embedded_html(httpd_req_t *req, const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    set_no_store(req);
    set_connection_close(req);
    return httpd_resp_send(req, (const char *)start, app_web_embedded_text_size(start, end));
}

esp_err_t redirect_to_login(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    set_no_store(req);
    set_connection_close(req);
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t read_form_body(httpd_req_t *req, char *body, size_t body_size)
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

esp_err_t get_form_value_decoded(const char *body, const char *key, char *out, size_t out_size)
{
    char encoded[APP_WEB_PARAM_FORM_BUF_SIZE];
    if (httpd_query_key_value(body, key, encoded, sizeof(encoded)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    return url_decode(out, out_size, encoded);
}
