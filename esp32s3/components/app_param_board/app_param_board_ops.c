#include "app_param_board_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

int board_connect(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t op_id)
{
    memset(send, 0, sizeof(*send));
    for (size_t i = 0; i < sizeof(send->data); i++) {
        send->data[i] = (uint8_t)(i + 1);
    }
    frame_prepare(send, FLASH_GET_DNA_CMD, 0);

    while (true) {
        if (param_board_recv_cancel(op_id)) {
            return -2;
        }
        memset(recv, 0, sizeof(*recv));
        uart_write_bytes(PARAM_BOARD_UART_NUM, (const char *)send, sizeof(*send));
        vTaskDelay(pdMS_TO_TICKS(1));
        int ret = frame_recv(recv, FLASH_GET_DNA_CMD, pdMS_TO_TICKS(3));
        if (ret == 0) {
            ESP_LOGI(APP_PARAM_BOARD_TAG, "board handshake ok");
            return 0;
        }
    }
    return -1;
}

static int run_read_operation(uint32_t op_id,
                              const uint16_t defaults[APP_PARAM_BOARD_PARAM_COUNT],
                              uint16_t values[APP_PARAM_BOARD_PARAM_COUNT],
                              char *err_msg,
                              size_t err_msg_size)
{
    param_uart_frame_t send = {0};
    param_uart_frame_t recv = {0};
    uint32_t store_addr = 0;

    status_message(op_id, "正在连接参数板卡");
    int ret = board_connect(&send, &recv, op_id);
    if (ret != 0) {
        set_err(err_msg, err_msg_size, ret == -2 ? "已取消连接参数板卡" : "连接参数板卡失败");
        return ret;
    }

    status_message(op_id, "正在读取参数存储地址");
    ret = get_param_store_addr(&send, &recv, &store_addr);
    if (ret == 1) {
        if (defaults == NULL) {
            set_err(err_msg, err_msg_size, "板卡参数区未初始化");
            return ret;
        }
        status_message(op_id, "正在初始化参数存储地址");
        ret = init_param_store_addr(&send, &recv, &store_addr);
        if (ret == 0) {
            status_message(op_id, "正在写入缺省参数");
            ret = write_params_at(&send, &recv, store_addr, defaults);
        }
    }
    if (ret != 0) {
        set_err(err_msg, err_msg_size, "读取参数存储地址失败");
        return ret;
    }

    status_message(op_id, "正在回读参数");
    ret = read_params_at(&send, &recv, store_addr, values);
    if (ret != 0) {
        set_err(err_msg, err_msg_size, ret == 1 ? "板卡参数区未完整初始化" : "参数回读失败");
        return ret;
    }
    return 0;
}

static int run_write_operation(uint32_t op_id,
                               const uint16_t values[APP_PARAM_BOARD_PARAM_COUNT],
                               char *err_msg,
                               size_t err_msg_size)
{
    param_uart_frame_t send = {0};
    param_uart_frame_t recv = {0};
    uint32_t store_addr = 0;

    status_message(op_id, "正在连接参数板卡");
    int ret = board_connect(&send, &recv, op_id);
    if (ret != 0) {
        set_err(err_msg, err_msg_size, ret == -2 ? "已取消连接参数板卡" : "连接参数板卡失败");
        return ret;
    }

    status_message(op_id, "正在读取参数存储地址");
    ret = get_param_store_addr(&send, &recv, &store_addr);
    if (ret == 1) {
        status_message(op_id, "正在初始化参数存储地址");
        ret = init_param_store_addr(&send, &recv, &store_addr);
    }
    if (ret != 0) {
        set_err(err_msg, err_msg_size, "获取参数存储地址失败");
        return ret;
    }

    status_message(op_id, "正在下载参数");
    ret = write_params_at(&send, &recv, store_addr, values);
    if (ret != 0) {
        set_err(err_msg, err_msg_size, "参数下载失败");
        return ret;
    }
    return 0;
}

static int run_flash_dump_operation(uint32_t op_id, char *err_msg, size_t err_msg_size)
{
    param_uart_frame_t send = {0};
    param_uart_frame_t recv = {0};
    uint8_t frame_bytes[PARAM_FLASH_DUMP_FRAME_BYTES];
    size_t bytes_read = 0;
    size_t last_report = 0;
    uint32_t addr = 0;

    if (s_flash_dump_data != NULL) {
        heap_caps_free(s_flash_dump_data);
        s_flash_dump_data = NULL;
    }
    s_flash_dump_bytes_read = 0;
    s_flash_dump_size = APP_PARAM_BOARD_FLASH_DUMP_SIZE;
    s_flash_dump_ready = false;

    s_flash_dump_data = heap_caps_malloc(APP_PARAM_BOARD_FLASH_DUMP_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_flash_dump_data == NULL) {
        set_err(err_msg, err_msg_size, "PSRAM 不足，无法缓存 flash dump");
        return -1;
    }

    flash_dump_status_update(op_id, 0, "正在连接参数板卡");
    int ret = board_connect(&send, &recv, op_id);
    if (ret != 0) {
        set_err(err_msg, err_msg_size, ret == -2 ? "已取消连接参数板卡" : "连接参数板卡失败");
        heap_caps_free(s_flash_dump_data);
        s_flash_dump_data = NULL;
        return ret;
    }

    while (bytes_read < APP_PARAM_BOARD_FLASH_DUMP_SIZE) {
        if (param_board_recv_cancel(op_id)) {
            set_err(err_msg, err_msg_size, "已取消 flash dump");
            heap_caps_free(s_flash_dump_data);
            s_flash_dump_data = NULL;
            return -2;
        }

        memset(&send, 0, sizeof(send));
        ret = flash_dump_read_packet(&send, &recv, addr);
        if (ret != 0) {
            snprintf(err_msg,
                     err_msg_size,
                     "flash dump 读取失败：addr=%lu",
                     (unsigned long)addr);
            heap_caps_free(s_flash_dump_data);
            s_flash_dump_data = NULL;
            return ret;
        }

        memcpy(frame_bytes, recv.data, sizeof(recv.data));
        memcpy(frame_bytes + sizeof(recv.data), recv.data_crc, sizeof(recv.data_crc));

        size_t remaining = APP_PARAM_BOARD_FLASH_DUMP_SIZE - bytes_read;
        size_t copy_len = remaining < sizeof(frame_bytes) ? remaining : sizeof(frame_bytes);
        memcpy(s_flash_dump_data + bytes_read, frame_bytes, copy_len);
        bytes_read += copy_len;
        addr += PARAM_FLASH_DUMP_FRAME_BYTES;

        if (bytes_read - last_report >= 4096U || bytes_read == APP_PARAM_BOARD_FLASH_DUMP_SIZE) {
            char message[APP_PARAM_BOARD_ERR_MSG_SIZE];
            snprintf(message,
                     sizeof(message),
                     "正在读取 flash dump：%lu/%lu 字节",
                     (unsigned long)bytes_read,
                     (unsigned long)APP_PARAM_BOARD_FLASH_DUMP_SIZE);
            flash_dump_status_update(op_id, bytes_read, message);
            last_report = bytes_read;
        }
    }

    flash_dump_status_finish(op_id,
                             APP_PARAM_BOARD_OP_DONE,
                             "flash dump 完成",
                             APP_PARAM_BOARD_FLASH_DUMP_SIZE,
                             true);
    return 0;
}

static void run_queued_operation(const param_board_cmd_t *cmd)
{
    char err_msg[APP_PARAM_BOARD_ERR_MSG_SIZE] = {0};
    uint16_t values[APP_PARAM_BOARD_PARAM_COUNT] = {0};
    int ret = -1;

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        status_set(cmd->id, APP_PARAM_BOARD_OP_KIND_NONE, APP_PARAM_BOARD_OP_FAILED, "参数板卡忙", NULL);
        return;
    }

    if (cmd->type == PARAM_BOARD_CMD_READ) {
        ret = run_read_operation(cmd->id, cmd->values, values, err_msg, sizeof(err_msg));
    } else if (cmd->type == PARAM_BOARD_CMD_WRITE) {
        ret = run_write_operation(cmd->id, cmd->values, err_msg, sizeof(err_msg));
    } else if (cmd->type == PARAM_BOARD_CMD_FLASH_DUMP) {
        ret = run_flash_dump_operation(cmd->id, err_msg, sizeof(err_msg));
    }

    xSemaphoreGive(s_lock);

    if (cmd->type == PARAM_BOARD_CMD_FLASH_DUMP) {
        if (ret == 0) {
            return;
        }
        flash_dump_status_finish(cmd->id,
                                 ret == -2 ? APP_PARAM_BOARD_OP_CANCELED : APP_PARAM_BOARD_OP_FAILED,
                                 err_msg[0] ? err_msg : "flash dump 失败",
                                 s_flash_dump_bytes_read,
                                 false);
        return;
    }

    if (ret == 0) {
        if (cmd->type == PARAM_BOARD_CMD_READ) {
            status_set(cmd->id, APP_PARAM_BOARD_OP_KIND_READ, APP_PARAM_BOARD_OP_DONE, "参数回读成功", values);
        } else {
            status_set(cmd->id, APP_PARAM_BOARD_OP_KIND_WRITE, APP_PARAM_BOARD_OP_DONE, "参数下载成功", NULL);
        }
    } else if (ret == -2) {
        status_set(cmd->id,
                   cmd->type == PARAM_BOARD_CMD_READ ? APP_PARAM_BOARD_OP_KIND_READ : APP_PARAM_BOARD_OP_KIND_WRITE,
                   APP_PARAM_BOARD_OP_CANCELED,
                   err_msg[0] ? err_msg : "已取消连接参数板卡",
                   NULL);
    } else {
        status_set(cmd->id,
                   cmd->type == PARAM_BOARD_CMD_READ ? APP_PARAM_BOARD_OP_KIND_READ : APP_PARAM_BOARD_OP_KIND_WRITE,
                   APP_PARAM_BOARD_OP_FAILED,
                   err_msg[0] ? err_msg : "参数板卡操作失败",
                   NULL);
    }
}

void app_param_board_task(void *arg)
{
    (void)arg;

    param_board_cmd_t cmd;
    while (true) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (cmd.type == PARAM_BOARD_CMD_CANCEL) {
            continue;
        }
        run_queued_operation(&cmd);
    }
}
