#include "app_web_file_server.h"

#include "app_web_assets.h"
#include "app_web_auth.h"
#include "app_web_common.h"
#include "app_web_files.h"
#include "app_web_flash_dump_api.h"
#include "app_web_pages.h"
#include "app_web_param_api.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_idf_version.h"

static httpd_handle_t s_server;

esp_err_t app_web_file_server_start(const char *mount_point)
{
    ESP_RETURN_ON_FALSE(mount_point != NULL && mount_point[0] == '/', ESP_ERR_INVALID_ARG,
                        APP_WEB_TAG, "invalid mount point");

    if (s_server != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(app_web_set_mount_point(mount_point), APP_WEB_TAG, "set mount point failed");

    log_heap_state("启动Web服务器前");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 12288;
    config.max_uri_handlers = 24;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 60;
    config.send_wait_timeout = 30;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    config.keep_alive_enable = false;
#endif

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), APP_WEB_TAG, "httpd_start failed");
    log_heap_state("启动Web服务器后");

    const httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    const httpd_uri_t app_uri = {.uri = "/app", .method = HTTP_GET, .handler = app_handler};
    const httpd_uri_t login_uri = {.uri = "/api/login", .method = HTTP_POST, .handler = login_handler};
    const httpd_uri_t logout_uri = {.uri = "/api/logout", .method = HTTP_POST, .handler = logout_handler};
    const httpd_uri_t auth_status_uri = {.uri = "/api/auth/status", .method = HTTP_GET, .handler = auth_status_handler};
    const httpd_uri_t app_info_uri = {.uri = "/api/app/info", .method = HTTP_GET, .handler = app_info_handler};
    const httpd_uri_t favicon_uri = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler};
    const httpd_uri_t assets_uri = {.uri = "/assets/*", .method = HTTP_GET, .handler = assets_handler};
    const httpd_uri_t files_uri = {.uri = "/files", .method = HTTP_GET, .handler = files_handler};
    const httpd_uri_t download_uri = {.uri = "/download", .method = HTTP_GET, .handler = download_handler};
    const httpd_uri_t upload_uri = {.uri = "/upload", .method = HTTP_POST, .handler = upload_handler};
    const httpd_uri_t delete_uri = {.uri = "/delete", .method = HTTP_POST, .handler = delete_handler};
    const httpd_uri_t bin_parse_uri = {.uri = "/api/bin/parse", .method = HTTP_GET, .handler = bin_parse_handler};
    const httpd_uri_t param_readback_uri = {.uri = "/api/param/readback", .method = HTTP_POST, .handler = param_readback_handler};
    const httpd_uri_t param_connect_cancel_uri = {.uri = "/api/param/connect/cancel", .method = HTTP_POST, .handler = param_connect_cancel_handler};
    const httpd_uri_t param_status_uri = {.uri = "/api/param/status", .method = HTTP_GET, .handler = param_status_handler};
    const httpd_uri_t param_download_uri = {.uri = "/api/param/download", .method = HTTP_POST, .handler = param_download_handler};
    const httpd_uri_t flash_dump_start_uri = {.uri = "/api/flash-dump/start", .method = HTTP_POST, .handler = flash_dump_start_handler};
    const httpd_uri_t flash_dump_status_uri = {.uri = "/api/flash-dump/status", .method = HTTP_GET, .handler = flash_dump_status_handler};
    const httpd_uri_t flash_dump_data_uri = {.uri = "/api/flash-dump/data", .method = HTTP_GET, .handler = flash_dump_data_handler};

    esp_err_t ret = httpd_register_uri_handler(s_server, &index_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register / failed");
    ret = httpd_register_uri_handler(s_server, &app_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /app failed");
    ret = httpd_register_uri_handler(s_server, &login_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/login failed");
    ret = httpd_register_uri_handler(s_server, &logout_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/logout failed");
    ret = httpd_register_uri_handler(s_server, &auth_status_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/auth/status failed");
    ret = httpd_register_uri_handler(s_server, &app_info_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/app/info failed");
    ret = httpd_register_uri_handler(s_server, &favicon_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /favicon.ico failed");
    ret = httpd_register_uri_handler(s_server, &assets_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /assets/* failed");
    ret = httpd_register_uri_handler(s_server, &files_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /files failed");
    ret = httpd_register_uri_handler(s_server, &download_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /download failed");
    ret = httpd_register_uri_handler(s_server, &upload_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /upload failed");
    ret = httpd_register_uri_handler(s_server, &delete_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /delete failed");
    ret = httpd_register_uri_handler(s_server, &bin_parse_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/bin/parse failed");
    ret = httpd_register_uri_handler(s_server, &param_readback_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/param/readback failed");
    ret = httpd_register_uri_handler(s_server, &param_connect_cancel_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/param/connect/cancel failed");
    ret = httpd_register_uri_handler(s_server, &param_status_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/param/status failed");
    ret = httpd_register_uri_handler(s_server, &param_download_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/param/download failed");
    ret = httpd_register_uri_handler(s_server, &flash_dump_start_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/flash-dump/start failed");
    ret = httpd_register_uri_handler(s_server, &flash_dump_status_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/flash-dump/status failed");
    ret = httpd_register_uri_handler(s_server, &flash_dump_data_uri);
    ESP_GOTO_ON_ERROR(ret, err_stop, APP_WEB_TAG, "register /api/flash-dump/data failed");

    return ESP_OK;

err_stop:
    httpd_stop(s_server);
    s_server = NULL;
    return ret;
}
