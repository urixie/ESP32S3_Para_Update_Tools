#include "app_param_board_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

void set_err(char *err_msg, size_t err_msg_size, const char *msg)
{
    if (err_msg != NULL && err_msg_size > 0) {
        snprintf(err_msg, err_msg_size, "%s", msg != NULL ? msg : "参数板卡操作失败");
    }
}

bool op_state_is_running(app_param_board_op_state_t state)
{
    return state == APP_PARAM_BOARD_OP_RUNNING;
}

app_param_board_op_kind_t cmd_kind(param_board_cmd_type_t type)
{
    switch (type) {
    case PARAM_BOARD_CMD_READ:
        return APP_PARAM_BOARD_OP_KIND_READ;
    case PARAM_BOARD_CMD_WRITE:
        return APP_PARAM_BOARD_OP_KIND_WRITE;
    case PARAM_BOARD_CMD_FLASH_DUMP:
        return APP_PARAM_BOARD_OP_KIND_FLASH_DUMP;
    case PARAM_BOARD_CMD_CANCEL:
    default:
        return APP_PARAM_BOARD_OP_KIND_NONE;
    }
}

const char *cmd_wait_message(param_board_cmd_type_t type)
{
    switch (type) {
    case PARAM_BOARD_CMD_READ:
        return "等待参数回读任务执行";
    case PARAM_BOARD_CMD_WRITE:
        return "等待参数下载任务执行";
    case PARAM_BOARD_CMD_FLASH_DUMP:
        return "等待 flash dump 任务执行";
    case PARAM_BOARD_CMD_CANCEL:
    default:
        return "等待参数板卡任务执行";
    }
}

void status_set(uint32_t id,
                       app_param_board_op_kind_t kind,
                       app_param_board_op_state_t state,
                       const char *message,
                       const uint16_t *values)
{
    if (s_state_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_state_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    s_status.id = id;
    s_status.kind = kind;
    s_status.state = state;
    s_status.has_values = (values != NULL);
    if (message != NULL) {
        snprintf(s_status.message, sizeof(s_status.message), "%s", message);
    } else {
        s_status.message[0] = '\0';
    }
    if (values != NULL) {
        memcpy(s_status.values, values, sizeof(s_status.values));
    }

    xSemaphoreGive(s_state_lock);
}

void status_message(uint32_t id, const char *message)
{
    if (s_state_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_state_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_status.id == id && op_state_is_running(s_status.state)) {
        snprintf(s_status.message, sizeof(s_status.message), "%s", message != NULL ? message : "");
    }
    xSemaphoreGive(s_state_lock);
}

void flash_dump_status_update(uint32_t id, size_t bytes_read, const char *message)
{
    if (s_state_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_state_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_status.id == id &&
        s_status.kind == APP_PARAM_BOARD_OP_KIND_FLASH_DUMP &&
        op_state_is_running(s_status.state)) {
        s_flash_dump_bytes_read = bytes_read;
        if (message != NULL) {
            snprintf(s_status.message, sizeof(s_status.message), "%s", message);
        }
    }
    xSemaphoreGive(s_state_lock);
}

void flash_dump_status_finish(uint32_t id,
                                     app_param_board_op_state_t state,
                                     const char *message,
                                     size_t bytes_read,
                                     bool data_ready)
{
    if (s_state_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_state_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_status.id = id;
    s_status.kind = APP_PARAM_BOARD_OP_KIND_FLASH_DUMP;
    s_status.state = state;
    s_status.has_values = false;
    snprintf(s_status.message, sizeof(s_status.message), "%s", message != NULL ? message : "");
    s_flash_dump_bytes_read = bytes_read;
    s_flash_dump_size = APP_PARAM_BOARD_FLASH_DUMP_SIZE;
    s_flash_dump_ready = data_ready;
    xSemaphoreGive(s_state_lock);
}

bool param_board_recv_cancel(uint32_t op_id)
{
    if (s_cmd_queue == NULL) {
        return false;
    }

    param_board_cmd_t cmd;
    bool canceled = false;
    while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
        if (cmd.type == PARAM_BOARD_CMD_CANCEL && (cmd.id == 0 || cmd.id == op_id)) {
            canceled = true;
        } else {
            ESP_LOGW(APP_PARAM_BOARD_TAG, "drop command while parameter operation is busy: type=%d id=%lu",
                     (int)cmd.type, (unsigned long)cmd.id);
        }
    }
    return canceled;
}
