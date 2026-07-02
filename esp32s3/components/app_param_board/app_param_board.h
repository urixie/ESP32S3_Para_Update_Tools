#ifndef APP_PARAM_BOARD_H
#define APP_PARAM_BOARD_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PARAM_BOARD_PARAM_COUNT 72
#define APP_PARAM_BOARD_ERR_MSG_SIZE 160
#define APP_PARAM_BOARD_FLASH_DUMP_SIZE (512U * 1024U)

typedef enum {
    APP_PARAM_BOARD_OP_IDLE = 0,
    APP_PARAM_BOARD_OP_RUNNING,
    APP_PARAM_BOARD_OP_DONE,
    APP_PARAM_BOARD_OP_FAILED,
    APP_PARAM_BOARD_OP_CANCELED,
} app_param_board_op_state_t;

typedef enum {
    APP_PARAM_BOARD_OP_KIND_NONE = 0,
    APP_PARAM_BOARD_OP_KIND_READ,
    APP_PARAM_BOARD_OP_KIND_WRITE,
    APP_PARAM_BOARD_OP_KIND_FLASH_DUMP,
} app_param_board_op_kind_t;

typedef struct {
    uint32_t id;
    app_param_board_op_state_t state;
    app_param_board_op_kind_t kind;
    char message[APP_PARAM_BOARD_ERR_MSG_SIZE];
    bool has_values;
    uint16_t values[APP_PARAM_BOARD_PARAM_COUNT];
} app_param_board_status_t;

typedef struct {
    uint32_t id;
    app_param_board_op_state_t state;
    char message[APP_PARAM_BOARD_ERR_MSG_SIZE];
    size_t bytes_read;
    size_t total_bytes;
    bool data_ready;
} app_param_board_flash_dump_status_t;

esp_err_t app_param_board_init(void);
esp_err_t app_param_board_start_read(const uint16_t defaults[APP_PARAM_BOARD_PARAM_COUNT],
                                     uint32_t *op_id,
                                     char *err_msg,
                                     size_t err_msg_size);
esp_err_t app_param_board_start_write(const uint16_t values[APP_PARAM_BOARD_PARAM_COUNT],
                                      uint32_t *op_id,
                                      char *err_msg,
                                      size_t err_msg_size);
esp_err_t app_param_board_start_flash_dump(uint32_t *op_id,
                                           char *err_msg,
                                           size_t err_msg_size);
esp_err_t app_param_board_get_status(uint32_t op_id,
                                     app_param_board_status_t *status,
                                     char *err_msg,
                                     size_t err_msg_size);
esp_err_t app_param_board_get_flash_dump_status(uint32_t op_id,
                                                app_param_board_flash_dump_status_t *status,
                                                char *err_msg,
                                                size_t err_msg_size);
esp_err_t app_param_board_lock_flash_dump_data(const uint8_t **data,
                                               size_t *size,
                                               char *err_msg,
                                               size_t err_msg_size);
void app_param_board_unlock_flash_dump_data(void);
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
