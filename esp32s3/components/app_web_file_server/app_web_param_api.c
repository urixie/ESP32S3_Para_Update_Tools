#include "app_web_param_api.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_param_bin.h"
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

static esp_err_t param_ns_to_board_tick(uint32_t value_ns,
                                        uint16_t *out_tick,
                                        char *err_msg,
                                        size_t err_msg_size)
{
    if (value_ns > APP_WEB_PARAM_MAX_NS_VALUE) {
        snprintf(err_msg, err_msg_size, "参数值必须在 0~%lu ns",
                 (unsigned long)APP_WEB_PARAM_MAX_NS_VALUE);
        return ESP_ERR_INVALID_ARG;
    }
    if ((value_ns % APP_WEB_PARAM_TIME_BASE_NS) != 0) {
        snprintf(err_msg, err_msg_size, "参数值必须是 %u ns 的整数倍",
                 (unsigned int)APP_WEB_PARAM_TIME_BASE_NS);
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t tick = value_ns / APP_WEB_PARAM_TIME_BASE_NS;
    if (tick > UINT16_MAX) {
        snprintf(err_msg, err_msg_size, "参数值超过板卡存储范围");
        return ESP_ERR_INVALID_ARG;
    }

    *out_tick = (uint16_t)tick;
    return ESP_OK;
}

static esp_err_t param_ns_values_to_board_ticks(const uint32_t values_ns[APP_PARAM_BIN_PARAM_COUNT],
                                                uint16_t board_values[APP_PARAM_BIN_PARAM_COUNT],
                                                char *err_msg,
                                                size_t err_msg_size)
{
    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        esp_err_t ret = param_ns_to_board_tick(values_ns[i], &board_values[i], err_msg, err_msg_size);
        if (ret != ESP_OK) {
            char detail[160];
            snprintf(detail, sizeof(detail), "参数地址 %u：%s", (unsigned int)i,
                     err_msg != NULL && err_msg[0] != '\0' ? err_msg : "参数值无效");
            snprintf(err_msg, err_msg_size, "%s", detail);
            return ret;
        }
    }
    return ESP_OK;
}

static uint32_t param_board_tick_to_ns(uint16_t value)
{
    return (uint32_t)value * APP_WEB_PARAM_TIME_BASE_NS;
}

static void build_param_defaults_ns(const app_param_bin_result_t *parsed, uint32_t defaults_ns[APP_PARAM_BIN_PARAM_COUNT])
{
    memset(defaults_ns, 0, sizeof(uint32_t) * APP_PARAM_BIN_PARAM_COUNT);
    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const app_param_bin_parameter_t *p = &parsed->parameters[i];
        if (p->address < APP_PARAM_BIN_PARAM_COUNT) {
            defaults_ns[p->address] = p->default_value;
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
                                    uint32_t values_ns[APP_PARAM_BIN_PARAM_COUNT],
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
        if (end == sep + 1 || *end != '\0' || value > APP_WEB_PARAM_MAX_NS_VALUE) {
            snprintf(err_msg, err_msg_size, "参数值无效");
            return ESP_ERR_INVALID_ARG;
        }
        uint16_t unused_tick = 0;
        esp_err_t ret = param_ns_to_board_tick((uint32_t)value, &unused_tick, err_msg, err_msg_size);
        if (ret != ESP_OK) {
            return ret;
        }
        if (!param_address_is_visible(parsed, (uint8_t)addr)) {
            snprintf(err_msg, err_msg_size, "参数地址未授权");
            return ESP_ERR_INVALID_ARG;
        }
        provided[addr] = true;
        values_ns[addr] = (uint32_t)value;
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

const char *param_board_state_name(app_param_board_op_state_t state)
{
    switch (state) {
    case APP_PARAM_BOARD_OP_IDLE:
        return "idle";
    case APP_PARAM_BOARD_OP_RUNNING:
        return "running";
    case APP_PARAM_BOARD_OP_DONE:
        return "done";
    case APP_PARAM_BOARD_OP_FAILED:
        return "failed";
    case APP_PARAM_BOARD_OP_CANCELED:
        return "canceled";
    default:
        return "unknown";
    }
}

static const char *param_board_kind_name(app_param_board_op_kind_t kind)
{
    switch (kind) {
    case APP_PARAM_BOARD_OP_KIND_READ:
        return "read";
    case APP_PARAM_BOARD_OP_KIND_WRITE:
        return "write";
    case APP_PARAM_BOARD_OP_KIND_FLASH_DUMP:
        return "flash_dump";
    case APP_PARAM_BOARD_OP_KIND_NONE:
    default:
        return "none";
    }
}

esp_err_t send_param_board_started_json(httpd_req_t *req, uint32_t op_id)
{
    char buf[96];
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"opId\":%lu,\"state\":\"running\"}", (unsigned long)op_id);
    return httpd_resp_sendstr(req, buf);
}

static esp_err_t send_param_board_status_json(httpd_req_t *req,
                                              const app_param_board_status_t *status,
                                              const app_param_bin_result_t *parsed)
{
    char buf[128];

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"opId\":%lu,\"state\":\"%s\",\"kind\":\"%s\",\"message\":\"",
             (unsigned long)status->id,
             param_board_state_name(status->state),
             param_board_kind_name(status->kind));
    httpd_resp_sendstr_chunk(req, buf);
    json_escape_send(req, status->message);
    httpd_resp_sendstr_chunk(req, "\"");

    if (status->state == APP_PARAM_BOARD_OP_FAILED || status->state == APP_PARAM_BOARD_OP_CANCELED) {
        httpd_resp_sendstr_chunk(req, ",\"error\":\"");
        json_escape_send(req, status->message);
        httpd_resp_sendstr_chunk(req, "\"");
    }

    if (status->has_values && parsed != NULL) {
        httpd_resp_sendstr_chunk(req, ",\"values\":[");
        bool first = true;
        for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
            const app_param_bin_parameter_t *p = &parsed->parameters[i];
            if (p->permission != APP_PARAM_BIN_PERMISSION_VISIBLE || p->address >= APP_PARAM_BOARD_PARAM_COUNT) {
                continue;
            }
            uint32_t value_ns = param_board_tick_to_ns(status->values[p->address]);
            char item[80];
            snprintf(item, sizeof(item), "%s{\"address\":%u,\"value\":%lu}",
                     first ? "" : ",", p->address, (unsigned long)value_ns);
            httpd_resp_sendstr_chunk(req, item);
            first = false;
        }
        httpd_resp_sendstr_chunk(req, "]");
    }

    httpd_resp_sendstr_chunk(req, "}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t param_readback_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_logged_in(req, NULL);
    if (auth_ret != ESP_OK) {
        return auth_ret;
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

    uint32_t defaults_ns[APP_PARAM_BIN_PARAM_COUNT];
    uint16_t board_defaults[APP_PARAM_BIN_PARAM_COUNT];
    build_param_defaults_ns(parsed, defaults_ns);
    ret = param_ns_values_to_board_ticks(defaults_ns, board_defaults, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        free(parsed);
        return send_json_error(req, "422 Unprocessable Entity", err_msg[0] ? err_msg : "板卡配置默认值无效");
    }

    uint32_t op_id = 0;
    ret = app_param_board_start_read(board_defaults, &op_id, err_msg, sizeof(err_msg));
    free(parsed);
    if (ret != ESP_OK) {
        return send_json_error(req,
                               ret == ESP_ERR_INVALID_STATE ? "409 Conflict" : "500 Internal Server Error",
                               err_msg[0] ? err_msg : "参数回读启动失败");
    }

    return send_param_board_started_json(req, op_id);
}

esp_err_t param_connect_cancel_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_logged_in(req, NULL);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    app_param_board_cancel_connect();
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t param_status_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_logged_in(req, NULL);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    char id_text[24] = {0};
    if (get_query_value(req, "id", id_text, sizeof(id_text)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少操作 ID");
    }

    char *end = NULL;
    unsigned long id = strtoul(id_text, &end, 10);
    if (end == id_text || *end != '\0' || id == 0 || id > UINT32_MAX) {
        return send_json_error(req, "400 Bad Request", "操作 ID 无效");
    }

    char err_msg[APP_PARAM_BOARD_ERR_MSG_SIZE] = {0};
    app_param_board_status_t status;
    esp_err_t ret = app_param_board_get_status((uint32_t)id, &status, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        return send_json_error(req,
                               ret == ESP_ERR_NOT_FOUND ? "404 Not Found" : "500 Internal Server Error",
                               err_msg[0] ? err_msg : "查询参数操作状态失败");
    }

    app_param_bin_result_t *parsed = NULL;
    if (status.has_values) {
        char encoded_path[APP_WEB_MAX_PATH] = {0};
        if (get_query_value(req, "path", encoded_path, sizeof(encoded_path)) != ESP_OK) {
            return send_json_error(req, "400 Bad Request", "缺少文件路径");
        }
        ret = load_parsed_bin(encoded_path, &parsed, err_msg, sizeof(err_msg));
        if (ret != ESP_OK) {
            return send_json_error(req,
                                   ret == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "422 Unprocessable Entity",
                                   err_msg[0] ? err_msg : "解析板卡配置失败");
        }
    }

    ret = send_param_board_status_json(req, &status, parsed);
    free(parsed);
    return ret;
}

esp_err_t param_download_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_logged_in(req, NULL);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    char body[APP_WEB_PARAM_FORM_BUF_SIZE];
    if (read_form_body(req, body, sizeof(body)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少请求内容");
    }

    char encoded_path[APP_WEB_MAX_PATH] = {0};
    if (httpd_query_key_value(body, "path", encoded_path, sizeof(encoded_path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "缺少文件路径");
    }

    esp_err_t ret;
    char values_text[1024] = {0};
    ret = get_form_value_decoded(body, "values", values_text, sizeof(values_text));
    if (ret != ESP_OK) {
        return send_json_error(req, "400 Bad Request",
                               ret == ESP_ERR_NOT_FOUND ? "缺少参数值" : "参数值过长");
    }

    char err_msg[160] = {0};
    app_param_bin_result_t *parsed = NULL;
    ret = load_parsed_bin(encoded_path, &parsed, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        return send_json_error(req, ret == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "422 Unprocessable Entity",
                               err_msg[0] ? err_msg : "解析板卡配置失败");
    }

    bool provided[APP_PARAM_BIN_PARAM_COUNT] = {0};
    uint32_t submitted_ns[APP_PARAM_BIN_PARAM_COUNT] = {0};
    ret = parse_param_values(values_text, parsed, provided, submitted_ns, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        free(parsed);
        return send_json_error(req, "400 Bad Request", err_msg[0] ? err_msg : "参数值无效");
    }

    uint32_t values_ns[APP_PARAM_BIN_PARAM_COUNT];
    /*
     * 参数下载以加密 bin 中的 72 项默认值作为基础镜像：
     * - 网页和加密 bin 中的参数单位为 ns；
     * - 隐藏参数不会从网页提交，保持加密 bin 默认值；
     * - 可见参数通过网页提交 ns 值覆盖对应 address；
     * - 最终写入板卡前统一换算为 50ns tick；
     * - 不再通过回读值保留隐藏参数，避免隐藏参数被旧板卡状态污染。
     */
    build_param_defaults_ns(parsed, values_ns);

    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        if (provided[i]) {
            values_ns[i] = submitted_ns[i];
        }
    }

    uint16_t board_values[APP_PARAM_BIN_PARAM_COUNT];
    ret = param_ns_values_to_board_ticks(values_ns, board_values, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        free(parsed);
        return send_json_error(req, "400 Bad Request", err_msg[0] ? err_msg : "参数值无效");
    }

    uint32_t op_id = 0;
    ret = app_param_board_start_write(board_values, &op_id, err_msg, sizeof(err_msg));
    free(parsed);
    if (ret != ESP_OK) {
        return send_json_error(req,
                               ret == ESP_ERR_INVALID_STATE ? "409 Conflict" : "500 Internal Server Error",
                               err_msg[0] ? err_msg : "参数下载启动失败");
    }

    return send_param_board_started_json(req, op_id);
}
