#include "app_web_assets.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_web_auth.h"
#include "app_web_common.h"

typedef enum {
    APP_WEB_ASSET_AUTH_USER = 0,
    APP_WEB_ASSET_AUTH_ADMIN,
} app_web_asset_auth_t;

typedef struct {
    const char *uri;
    const uint8_t *start;
    const uint8_t *end;
    const char *content_type;
    app_web_asset_auth_t auth;
} app_web_asset_t;

extern const uint8_t admin_base_css_start[] asm("_binary_admin_base_css_start");
extern const uint8_t admin_base_css_end[] asm("_binary_admin_base_css_end");
extern const uint8_t admin_file_page_css_start[] asm("_binary_admin_file_page_css_start");
extern const uint8_t admin_file_page_css_end[] asm("_binary_admin_file_page_css_end");
extern const uint8_t admin_shell_theme_css_start[] asm("_binary_admin_shell_theme_css_start");
extern const uint8_t admin_shell_theme_css_end[] asm("_binary_admin_shell_theme_css_end");
extern const uint8_t admin_dialogs_css_start[] asm("_binary_admin_dialogs_css_start");
extern const uint8_t admin_dialogs_css_end[] asm("_binary_admin_dialogs_css_end");
extern const uint8_t admin_flash_dump_css_start[] asm("_binary_admin_flash_dump_css_start");
extern const uint8_t admin_flash_dump_css_end[] asm("_binary_admin_flash_dump_css_end");
extern const uint8_t admin_about_css_start[] asm("_binary_admin_about_css_start");
extern const uint8_t admin_about_css_end[] asm("_binary_admin_about_css_end");
extern const uint8_t admin_typography_css_start[] asm("_binary_admin_typography_css_start");
extern const uint8_t admin_typography_css_end[] asm("_binary_admin_typography_css_end");
extern const uint8_t admin_watermark_css_start[] asm("_binary_admin_watermark_css_start");
extern const uint8_t admin_watermark_css_end[] asm("_binary_admin_watermark_css_end");
extern const uint8_t admin_core_js_start[] asm("_binary_admin_core_js_start");
extern const uint8_t admin_core_js_end[] asm("_binary_admin_core_js_end");
extern const uint8_t admin_flash_dump_js_start[] asm("_binary_admin_flash_dump_js_start");
extern const uint8_t admin_flash_dump_js_end[] asm("_binary_admin_flash_dump_js_end");
extern const uint8_t admin_navigation_js_start[] asm("_binary_admin_navigation_js_start");
extern const uint8_t admin_navigation_js_end[] asm("_binary_admin_navigation_js_end");
extern const uint8_t admin_files_js_start[] asm("_binary_admin_files_js_start");
extern const uint8_t admin_files_js_end[] asm("_binary_admin_files_js_end");
extern const uint8_t admin_param_config_js_start[] asm("_binary_admin_param_config_js_start");
extern const uint8_t admin_param_config_js_end[] asm("_binary_admin_param_config_js_end");
extern const uint8_t admin_about_js_start[] asm("_binary_admin_about_js_start");
extern const uint8_t admin_about_js_end[] asm("_binary_admin_about_js_end");
extern const uint8_t admin_bootstrap_js_start[] asm("_binary_admin_bootstrap_js_start");
extern const uint8_t admin_bootstrap_js_end[] asm("_binary_admin_bootstrap_js_end");
extern const uint8_t user_app_css_start[] asm("_binary_user_app_css_start");
extern const uint8_t user_app_css_end[] asm("_binary_user_app_css_end");
extern const uint8_t user_app_js_start[] asm("_binary_user_app_js_start");
extern const uint8_t user_app_js_end[] asm("_binary_user_app_js_end");

static const app_web_asset_t s_assets[] = {
    {.uri = "/assets/admin_base.css", .start = admin_base_css_start, .end = admin_base_css_end,
     .content_type = "text/css; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_file_page.css", .start = admin_file_page_css_start, .end = admin_file_page_css_end,
     .content_type = "text/css; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_shell_theme.css", .start = admin_shell_theme_css_start, .end = admin_shell_theme_css_end,
     .content_type = "text/css; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_dialogs.css", .start = admin_dialogs_css_start, .end = admin_dialogs_css_end,
     .content_type = "text/css; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_flash_dump.css", .start = admin_flash_dump_css_start, .end = admin_flash_dump_css_end,
     .content_type = "text/css; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_about.css", .start = admin_about_css_start, .end = admin_about_css_end,
     .content_type = "text/css; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_typography.css", .start = admin_typography_css_start, .end = admin_typography_css_end,
     .content_type = "text/css; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_watermark.css", .start = admin_watermark_css_start, .end = admin_watermark_css_end,
     .content_type = "text/css; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_core.js", .start = admin_core_js_start, .end = admin_core_js_end,
     .content_type = "application/javascript; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_flash_dump.js", .start = admin_flash_dump_js_start, .end = admin_flash_dump_js_end,
     .content_type = "application/javascript; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_navigation.js", .start = admin_navigation_js_start, .end = admin_navigation_js_end,
     .content_type = "application/javascript; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_files.js", .start = admin_files_js_start, .end = admin_files_js_end,
     .content_type = "application/javascript; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_param_config.js", .start = admin_param_config_js_start, .end = admin_param_config_js_end,
     .content_type = "application/javascript; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_about.js", .start = admin_about_js_start, .end = admin_about_js_end,
     .content_type = "application/javascript; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/admin_bootstrap.js", .start = admin_bootstrap_js_start, .end = admin_bootstrap_js_end,
     .content_type = "application/javascript; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_ADMIN},
    {.uri = "/assets/user_app.css", .start = user_app_css_start, .end = user_app_css_end,
     .content_type = "text/css; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_USER},
    {.uri = "/assets/user_app.js", .start = user_app_js_start, .end = user_app_js_end,
     .content_type = "application/javascript; charset=utf-8", .auth = APP_WEB_ASSET_AUTH_USER},
};

static bool uri_matches(const char *request_uri, const char *asset_uri)
{
    size_t len = strlen(asset_uri);
    return strncmp(request_uri, asset_uri, len) == 0 &&
           (request_uri[len] == '\0' || request_uri[len] == '?');
}

static const app_web_asset_t *find_asset(const char *uri)
{
    for (size_t i = 0; i < sizeof(s_assets) / sizeof(s_assets[0]); i++) {
        if (uri_matches(uri, s_assets[i].uri)) {
            return &s_assets[i];
        }
    }
    return NULL;
}

esp_err_t assets_handler(httpd_req_t *req)
{
    const app_web_asset_t *asset = find_asset(req->uri);
    if (asset == NULL) {
        return send_json_error(req, "404 Not Found", "资源不存在");
    }

    esp_err_t auth_ret = asset->auth == APP_WEB_ASSET_AUTH_ADMIN ? require_admin(req) : require_logged_in(req, NULL);
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    set_no_store(req);
    set_connection_close(req);
    httpd_resp_set_type(req, asset->content_type);
    return httpd_resp_send(req, (const char *)asset->start, app_web_embedded_text_size(asset->start, asset->end));
}
