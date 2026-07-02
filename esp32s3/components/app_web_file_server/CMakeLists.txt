idf_component_register(
    SRCS "app_web_file_server.c"
         "app_web_auth.c"
         "app_web_common.c"
         "app_web_files.c"
         "app_web_flash_dump_api.c"
         "app_web_pages.c"
         "app_web_param_api.c"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format esp_http_server esp_netif app_storage_lock app_param_bin app_param_board
    EMBED_TXTFILES "login.html" "app.html" "user_app.html"
)
