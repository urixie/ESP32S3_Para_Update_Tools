#ifndef APP_PARAM_BIN_H
#define APP_PARAM_BIN_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PARAM_BIN_PARAM_COUNT 72
#define APP_PARAM_BIN_NAME_MAX_BYTES 96
#define APP_PARAM_BIN_NONCE_HEX_LEN 24

typedef enum {
    APP_PARAM_BIN_TYPE_CONTROL = 0,
    APP_PARAM_BIN_TYPE_PROTECTION = 1,
} app_param_bin_type_t;

typedef enum {
    APP_PARAM_BIN_PERMISSION_HIDDEN = 0,
    APP_PARAM_BIN_PERMISSION_VISIBLE = 1,
} app_param_bin_permission_t;

typedef struct {
    uint8_t address;
    uint16_t default_value;
    app_param_bin_type_t param_type;
    app_param_bin_permission_t permission;
    char name[APP_PARAM_BIN_NAME_MAX_BYTES + 1];
} app_param_bin_parameter_t;

typedef struct {
    uint16_t header_size;
    uint8_t format_version;
    char nonce_hex[APP_PARAM_BIN_NONCE_HEX_LEN + 1];
    uint32_t ciphertext_len;
    uint8_t tag_len;
    uint32_t file_size;
    app_param_bin_parameter_t parameters[APP_PARAM_BIN_PARAM_COUNT];
} app_param_bin_result_t;

esp_err_t app_param_bin_parse_file(const char *path,
                                   app_param_bin_result_t *out,
                                   char *err_msg,
                                   size_t err_msg_size);

const char *app_param_bin_type_name(app_param_bin_type_t type);
const char *app_param_bin_type_label(app_param_bin_type_t type);
const char *app_param_bin_permission_name(app_param_bin_permission_t permission);
const char *app_param_bin_permission_label(app_param_bin_permission_t permission);

#ifdef __cplusplus
}
#endif

#endif
