#include "app_web_files.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_storage_lock.h"
#include "app_web_auth.h"
#include "app_web_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

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
