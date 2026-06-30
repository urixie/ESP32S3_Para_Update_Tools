#ifndef APP_PARAM_BOARD_H
#define APP_PARAM_BOARD_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PARAM_BOARD_PARAM_COUNT 72

esp_err_t app_param_board_init(void);
esp_err_t app_param_board_read(const uint16_t defaults[APP_PARAM_BOARD_PARAM_COUNT],
                               uint16_t values[APP_PARAM_BOARD_PARAM_COUNT],
                               char *err_msg,
                               size_t err_msg_size);
esp_err_t app_param_board_write(const uint16_t values[APP_PARAM_BOARD_PARAM_COUNT],
                                char *err_msg,
                                size_t err_msg_size);
void app_param_board_cancel_connect(void);

#ifdef __cplusplus
}
#endif

#endif
