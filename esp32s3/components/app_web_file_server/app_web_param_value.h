#ifndef APP_WEB_PARAM_VALUE_H
#define APP_WEB_PARAM_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_param_bin.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t param_ns_values_to_board_ticks(const uint32_t values_ns[APP_PARAM_BIN_PARAM_COUNT],
                                         uint16_t board_values[APP_PARAM_BIN_PARAM_COUNT],
                                         char *err_msg,
                                         size_t err_msg_size);
uint32_t param_board_tick_to_ns(uint16_t value);
void build_param_defaults_ns(const app_param_bin_result_t *parsed, uint32_t defaults_ns[APP_PARAM_BIN_PARAM_COUNT]);
esp_err_t parse_param_values(const char *text,
                             const app_param_bin_result_t *parsed,
                             bool provided[APP_PARAM_BIN_PARAM_COUNT],
                             uint32_t values_ns[APP_PARAM_BIN_PARAM_COUNT],
                             char *err_msg,
                             size_t err_msg_size);

#ifdef __cplusplus
}
#endif

#endif
