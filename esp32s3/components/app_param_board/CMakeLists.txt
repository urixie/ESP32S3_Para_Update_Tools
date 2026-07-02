idf_component_register(
    SRCS "app_param_board.c"
         "app_param_board_frame.c"
         "app_param_board_ops.c"
         "app_param_board_state.c"
         "app_param_board_store.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_uart esp_driver_gpio
)
