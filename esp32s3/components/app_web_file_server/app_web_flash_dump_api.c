#include "app_web_flash_dump_api.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_param_board.h"
#include "app_web_auth.h"
#include "app_web_common.h"
#include "app_web_param_api.h"

esp_err_t flash_dump_start_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_admin(req);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    char err_msg[APP_PARAM_BOARD_ERR_MSG_SIZE] = {0};
    uint32_t op_id = 0;
    esp_err_t ret = app_param_board_start_flash_dump(&op_id, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        return send_json_error(req,
                               ret == ESP_ERR_INVALID_STATE ? "409 Conflict" : "500 Internal Server Error",
                               err_msg[0] ? err_msg : "flash dump 启动失败");
    }

    return send_param_board_started_json(req, op_id);
}

static esp_err_t send_flash_dump_status_json(httpd_req_t *req,
                                             const app_param_board_flash_dump_status_t *status)
{
    char buf[256];
    unsigned int percent = 0;
    if (status->total_bytes > 0) {
        percent = (unsigned int)(((uint64_t)status->bytes_read * 100U) / status->total_bytes);
        if (percent > 100U) {
            percent = 100U;
        }
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    set_connection_close(req);
    snprintf(buf,
             sizeof(buf),
             "{\"ok\":true,\"opId\":%lu,\"state\":\"%s\",\"kind\":\"flash_dump\","
             "\"bytesRead\":%lu,\"totalBytes\":%lu,\"percent\":%u,\"dataReady\":%s,\"message\":\"",
             (unsigned long)status->id,
             param_board_state_name(status->state),
             (unsigned long)status->bytes_read,
             (unsigned long)status->total_bytes,
             percent,
             status->data_ready ? "true" : "false");
    httpd_resp_sendstr_chunk(req, buf);
    json_escape_send(req, status->message);
    httpd_resp_sendstr_chunk(req, "\"");
    if (status->state == APP_PARAM_BOARD_OP_FAILED || status->state == APP_PARAM_BOARD_OP_CANCELED) {
        httpd_resp_sendstr_chunk(req, ",\"error\":\"");
        json_escape_send(req, status->message);
        httpd_resp_sendstr_chunk(req, "\"");
    }
    httpd_resp_sendstr_chunk(req, "}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t flash_dump_status_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_admin(req);
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
    app_param_board_flash_dump_status_t status;
    esp_err_t ret = app_param_board_get_flash_dump_status((uint32_t)id, &status, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        return send_json_error(req,
                               ret == ESP_ERR_NOT_FOUND ? "404 Not Found" : "500 Internal Server Error",
                               err_msg[0] ? err_msg : "查询 flash dump 状态失败");
    }

    return send_flash_dump_status_json(req, &status);
}

esp_err_t flash_dump_data_handler(httpd_req_t *req)
{
    esp_err_t auth_ret = require_admin(req);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    const uint8_t *data = NULL;
    size_t size = 0;
    char err_msg[APP_PARAM_BOARD_ERR_MSG_SIZE] = {0};
    esp_err_t ret = app_param_board_lock_flash_dump_data(&data, &size, err_msg, sizeof(err_msg));
    if (ret != ESP_OK) {
        return send_json_error(req,
                               ret == ESP_ERR_INVALID_STATE ? "409 Conflict" : "503 Service Unavailable",
                               err_msg[0] ? err_msg : "flash dump 数据尚未准备好");
    }

    httpd_resp_set_type(req, "application/octet-stream");
    set_connection_close(req);
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"flash_dump_512kb.bin\"");

    esp_err_t send_ret = ESP_OK;
    for (size_t offset = 0; offset < size;) {
        size_t chunk = size - offset;
        if (chunk > APP_WEB_IO_BUF_SIZE) {
            chunk = APP_WEB_IO_BUF_SIZE;
        }
        if (httpd_resp_send_chunk(req, (const char *)data + offset, chunk) != ESP_OK) {
            send_ret = ESP_FAIL;
            break;
        }
        offset += chunk;
    }
    if (send_ret == ESP_OK) {
        send_ret = httpd_resp_send_chunk(req, NULL, 0);
    }

    app_param_board_unlock_flash_dump_data();
    return send_ret;
}
