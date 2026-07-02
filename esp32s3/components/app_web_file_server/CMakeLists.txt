idf_component_register(
    SRCS "app_web_file_server.c"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format esp_http_server esp_netif app_storage_lock app_param_bin app_param_board
    EMBED_TXTFILES "login.html" "app.html" "user_app.html"
)
