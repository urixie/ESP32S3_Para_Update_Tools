#include "app_web_files.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_storage_lock.h"
#include "app_web_auth.h"
#include "app_web_common.h"
#include "app_web_file_meta.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

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
