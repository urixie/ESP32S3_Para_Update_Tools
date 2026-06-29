idf_component_register(
    SRCS "app_param_board.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_uart esp_driver_gpio
)
