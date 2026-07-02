#include "app_web_param_bin_api.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_storage_lock.h"
#include "app_web_auth.h"
#include "app_web_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static void send_visible_param_json(httpd_req_t *req, const app_param_bin_parameter_t *p, bool first)
{
    char buf[192];
    snprintf(buf, sizeof(buf), "%s{\"name\":\"", first ? "" : ",");
    httpd_resp_sendstr_chunk(req, buf);
    json_escape_send(req, p->name);
    snprintf(buf, sizeof(buf),
             "\",\"address\":%u,\"defaultValue\":%lu,\"paramType\":\"%s\",\"paramTypeLabel\":\"%s\"}",
             p->address,
             (unsigned long)p->default_value,
             app_param_bin_type_name(p->param_type),
             app_param_bin_type_label(p->param_type));
    httpd_resp_sendstr_chunk(req, buf);
}

esp_err_t bin_parse_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_logged_in(req, NULL);
    if (auth_ret != ESP_OK) {
        return auth_ret;
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
        ESP_LOGW(APP_WEB_TAG, "parse failed %s: %s", full_path, parse_error);
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

esp_err_t load_parsed_bin(const char *encoded_path,
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
