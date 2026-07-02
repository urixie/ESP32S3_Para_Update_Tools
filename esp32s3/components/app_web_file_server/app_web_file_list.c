#include "app_web_files.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app_param_bin.h"
#include "app_storage_lock.h"
#include "app_web_auth.h"
#include "app_web_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

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
