#include "app_web_files.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_param_bin.h"
#include "app_storage_lock.h"
#include "app_web_auth.h"
#include "app_web_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

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
    int len = snprintf(full_path, sizeof(full_path), "%s%s", app_web_mount_point(), relative_path);
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
        ESP_LOGW(APP_WEB_TAG, "skip board name for %s: %s", full_path, parse_error);
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
                  ? snprintf(full_dir, APP_WEB_MAX_PATH, "%s", app_web_mount_point())
                  : snprintf(full_dir, APP_WEB_MAX_PATH, "%s/%s", app_web_mount_point(), relative_dir);
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

        int full_len = snprintf(full_path, APP_WEB_MAX_PATH, "%s%s", app_web_mount_point(), relative_path);
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

esp_err_t files_handler(httpd_req_t *req)
{
    log_heap_state("读取文件列表前");
    esp_err_t auth_ret = require_logged_in(req, NULL);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);

    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }

    DIR *dir = opendir(app_web_mount_point());
    if (dir == NULL) {
        int err = errno;
        app_storage_unlock();
        ESP_LOGE(APP_WEB_TAG, "opendir failed: errno=%d", err);
        return send_json_error(req, "500 Internal Server Error", "无法打开存储目录");
    }
    closedir(dir);

    httpd_resp_sendstr_chunk(req, "{\"mount_point\":\"");
    json_escape_send(req, app_web_mount_point());
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

esp_err_t download_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_admin(req);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    char raw_path[APP_WEB_MAX_PATH];
    char full_path[APP_WEB_MAX_PATH];
    if (get_query_value(req, "path", raw_path, sizeof(raw_path)) != ESP_OK ||
        build_storage_path(raw_path, false, full_path, sizeof(full_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "路径无效");
    }

    ESP_LOGI(APP_WEB_TAG, "download start: %s", full_path);
    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }
    esp_err_t ret = stream_file(req, full_path, true);
    app_storage_unlock();
    ESP_LOGI(APP_WEB_TAG, "download done: %s", full_path);
    return ret;
}

esp_err_t upload_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_admin(req);
    if (auth_ret != ESP_OK) {
        return auth_ret;
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
    int len = snprintf(full_path, sizeof(full_path), "%s/%s", app_web_mount_point(), filename);
    if (len <= 0 || (size_t)len >= sizeof(full_path)) {
        return send_json_error(req, "400 Bad Request", "文件名过长");
    }

    char tmp_path[APP_WEB_MAX_PATH];
    len = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", full_path);
    if (len <= 0 || (size_t)len >= sizeof(tmp_path)) {
        return send_json_error(req, "400 Bad Request", "文件名过长");
    }

    log_heap_state("文件上传开始");
    ESP_LOGI(APP_WEB_TAG, "upload start: %s, tmp=%s, size=%zu", full_path, tmp_path, req->content_len);
    if (app_storage_lock(portMAX_DELAY) != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "文件系统忙");
    }

    FILE *f = fopen(tmp_path, "wb");
    if (f == NULL) {
        int err = errno;
        app_storage_unlock();
        ESP_LOGE(APP_WEB_TAG, "open tmp upload file failed: %s, errno=%d", tmp_path, err);
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
                ESP_LOGW(APP_WEB_TAG, "upload recv timeout %d/%d, received=%zu/%zu",
                         timeout_count, max_timeout_count, received, total);
                continue;
            }
            ESP_LOGE(APP_WEB_TAG, "upload recv failed: recv_len=%d, received=%zu/%zu", recv_len, received, total);
            ret = recv_len == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
            break;
        }

        timeout_count = 0;
        if (fwrite(buf, 1, recv_len, f) != (size_t)recv_len) {
            int err = errno;
            ESP_LOGE(APP_WEB_TAG, "upload write failed, received=%zu/%zu, errno=%d", received, total, err);
            ret = ESP_FAIL;
            break;
        }

        remaining -= recv_len;
        received += recv_len;
        if (received == total || received - last_log >= 64 * 1024) {
            ESP_LOGI(APP_WEB_TAG, "upload progress: %zu/%zu", received, total);
            last_log = received;
        }
    }

    free(buf);

    if (ret == ESP_OK && received != total) {
        ESP_LOGE(APP_WEB_TAG, "upload size mismatch: received=%zu/%zu", received, total);
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK && fflush(f) != 0) {
        int err = errno;
        ESP_LOGE(APP_WEB_TAG, "upload fflush failed errno=%d", err);
        ret = ESP_FAIL;
    }

    if (fclose(f) != 0 && ret == ESP_OK) {
        int err = errno;
        ESP_LOGE(APP_WEB_TAG, "upload fclose failed errno=%d", err);
        ret = ESP_FAIL;
    }
    f = NULL;

    if (ret == ESP_OK) {
        remove(full_path);
        if (rename(tmp_path, full_path) != 0) {
            int err = errno;
            ESP_LOGE(APP_WEB_TAG, "upload rename failed: %s -> %s, errno=%d", tmp_path, full_path, err);
            ret = ESP_FAIL;
        }
    }

    if (ret != ESP_OK) {
        remove(tmp_path);
    }

    app_storage_unlock();

    if (ret != ESP_OK) {
        ESP_LOGE(APP_WEB_TAG, "upload failed: %s, received=%zu/%zu, err=%s",
                 full_path, received, total, esp_err_to_name(ret));
        return send_json_error(req, "500 Internal Server Error", "上传失败");
    }

    log_heap_state("文件上传完成");
    ESP_LOGI(APP_WEB_TAG, "upload done: %s, received=%zu", full_path, received);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t delete_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_admin(req);
    if (auth_ret != ESP_OK) {
        return auth_ret;
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

    ESP_LOGI(APP_WEB_TAG, "delete start: %s", full_path);
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
        ESP_LOGE(APP_WEB_TAG, "delete failed errno=%d", err);
        return send_json_error(req, "500 Internal Server Error", "删除失败");
    }

    app_storage_unlock();
    ESP_LOGI(APP_WEB_TAG, "delete done: %s", full_path);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
