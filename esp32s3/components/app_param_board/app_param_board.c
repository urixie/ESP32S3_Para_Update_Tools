#include "app_param_board.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_param_board_internal.h"
#include "esp_check.h"
#include "esp_log.h"

static bool s_uart_ready;
SemaphoreHandle_t s_lock;
SemaphoreHandle_t s_state_lock;
QueueHandle_t s_cmd_queue;
static TaskHandle_t s_task_handle;
static uint32_t s_next_op_id = 1;
uint8_t *s_flash_dump_data;
size_t s_flash_dump_bytes_read;
size_t s_flash_dump_size;
bool s_flash_dump_ready;
app_param_board_status_t s_status = {
    .id = 0,
    .state = APP_PARAM_BOARD_OP_IDLE,
    .kind = APP_PARAM_BOARD_OP_KIND_NONE,
};

esp_err_t app_param_board_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, APP_PARAM_BOARD_TAG, "create mutex failed");
    }
    if (s_state_lock == NULL) {
        s_state_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_state_lock != NULL, ESP_ERR_NO_MEM, APP_PARAM_BOARD_TAG, "create state mutex failed");
    }
    if (s_cmd_queue == NULL) {
        s_cmd_queue = xQueueCreate(PARAM_BOARD_QUEUE_LEN, sizeof(param_board_cmd_t));
        ESP_RETURN_ON_FALSE(s_cmd_queue != NULL, ESP_ERR_NO_MEM, APP_PARAM_BOARD_TAG, "create command queue failed");
    }
    if (s_task_handle == NULL) {
        BaseType_t ok = xTaskCreate(app_param_board_task,
                                    "param_board_task",
                                    PARAM_BOARD_TASK_STACK,
                                    NULL,
                                    PARAM_BOARD_TASK_PRIORITY,
                                    &s_task_handle);
        ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, APP_PARAM_BOARD_TAG, "create parameter task failed");
    }
    if (s_uart_ready) {
        return ESP_OK;
    }

    const uart_config_t uart_config = {
        .baud_rate = PARAM_BOARD_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_APB,
    };

    ESP_RETURN_ON_ERROR(uart_param_config(PARAM_BOARD_UART_NUM, &uart_config), APP_PARAM_BOARD_TAG, "uart config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(PARAM_BOARD_UART_NUM, PARAM_BOARD_TX_GPIO, PARAM_BOARD_RX_GPIO,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        APP_PARAM_BOARD_TAG, "uart pin config failed");
    ESP_RETURN_ON_ERROR(uart_driver_install(PARAM_BOARD_UART_NUM, PARAM_BOARD_RX_BUF_SIZE,
                                            PARAM_BOARD_TX_BUF_SIZE, PARAM_BOARD_UART_EVENT_QUEUE_SIZE, NULL, 0),
                        APP_PARAM_BOARD_TAG, "uart driver install failed");
    s_uart_ready = true;
    ESP_LOGI(APP_PARAM_BOARD_TAG, "UART%d ready: tx=%d rx=%d baud=%d",
             PARAM_BOARD_UART_NUM, PARAM_BOARD_TX_GPIO, PARAM_BOARD_RX_GPIO, PARAM_BOARD_BAUD_RATE);
    return ESP_OK;
}

static esp_err_t start_operation(param_board_cmd_type_t type,
                                 const uint16_t values[APP_PARAM_BOARD_PARAM_COUNT],
                                 uint32_t *op_id,
                                 char *err_msg,
                                 size_t err_msg_size)
{
    ESP_RETURN_ON_FALSE(type == PARAM_BOARD_CMD_READ ||
                            type == PARAM_BOARD_CMD_WRITE ||
                            type == PARAM_BOARD_CMD_FLASH_DUMP,
                        ESP_ERR_INVALID_ARG, APP_PARAM_BOARD_TAG, "invalid command type");
    ESP_RETURN_ON_FALSE(type == PARAM_BOARD_CMD_FLASH_DUMP || values != NULL,
                        ESP_ERR_INVALID_ARG, APP_PARAM_BOARD_TAG, "values is null");
    ESP_RETURN_ON_ERROR(app_param_board_init(), APP_PARAM_BOARD_TAG, "init failed");

    param_board_cmd_t cmd = {
        .type = type,
    };
    if (values != NULL) {
        memcpy(cmd.values, values, sizeof(cmd.values));
    }

    if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        set_err(err_msg, err_msg_size, "参数板卡状态忙");
        return ESP_ERR_TIMEOUT;
    }

    if (op_state_is_running(s_status.state)) {
        xSemaphoreGive(s_state_lock);
        set_err(err_msg, err_msg_size, "参数板卡忙");
        return ESP_ERR_INVALID_STATE;
    }

    cmd.id = s_next_op_id++;
    if (s_next_op_id == 0) {
        s_next_op_id = 1;
    }

    s_status.id = cmd.id;
    s_status.kind = cmd_kind(type);
    s_status.state = APP_PARAM_BOARD_OP_RUNNING;
    s_status.has_values = false;
    snprintf(s_status.message, sizeof(s_status.message), "%s", cmd_wait_message(type));
    if (type == PARAM_BOARD_CMD_FLASH_DUMP) {
        s_flash_dump_bytes_read = 0;
        s_flash_dump_size = APP_PARAM_BOARD_FLASH_DUMP_SIZE;
        s_flash_dump_ready = false;
    }
    xSemaphoreGive(s_state_lock);

    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        status_set(cmd.id, cmd_kind(type), APP_PARAM_BOARD_OP_FAILED, "参数任务队列忙", NULL);
        set_err(err_msg, err_msg_size, "参数任务队列忙");
        return ESP_ERR_TIMEOUT;
    }

    if (op_id != NULL) {
        *op_id = cmd.id;
    }
    return ESP_OK;
}

esp_err_t app_param_board_start_read(const uint16_t defaults[APP_PARAM_BOARD_PARAM_COUNT],
                                     uint32_t *op_id,
                                     char *err_msg,
                                     size_t err_msg_size)
{
    return start_operation(PARAM_BOARD_CMD_READ, defaults, op_id, err_msg, err_msg_size);
}

esp_err_t app_param_board_start_write(const uint16_t values[APP_PARAM_BOARD_PARAM_COUNT],
                                      uint32_t *op_id,
                                      char *err_msg,
                                      size_t err_msg_size)
{
    return start_operation(PARAM_BOARD_CMD_WRITE, values, op_id, err_msg, err_msg_size);
}

esp_err_t app_param_board_start_flash_dump(uint32_t *op_id, char *err_msg, size_t err_msg_size)
{
    return start_operation(PARAM_BOARD_CMD_FLASH_DUMP, NULL, op_id, err_msg, err_msg_size);
}

esp_err_t app_param_board_get_status(uint32_t op_id,
                                     app_param_board_status_t *status,
                                     char *err_msg,
                                     size_t err_msg_size)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, APP_PARAM_BOARD_TAG, "status is null");
    ESP_RETURN_ON_ERROR(app_param_board_init(), APP_PARAM_BOARD_TAG, "init failed");

    if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        set_err(err_msg, err_msg_size, "参数板卡状态忙");
        return ESP_ERR_TIMEOUT;
    }

    if (op_id != 0 && s_status.id != op_id) {
        xSemaphoreGive(s_state_lock);
        set_err(err_msg, err_msg_size, "参数操作不存在");
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(status, &s_status, sizeof(*status));
    xSemaphoreGive(s_state_lock);
    return ESP_OK;
}

esp_err_t app_param_board_get_flash_dump_status(uint32_t op_id,
                                                app_param_board_flash_dump_status_t *status,
                                                char *err_msg,
                                                size_t err_msg_size)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, APP_PARAM_BOARD_TAG, "status is null");
    ESP_RETURN_ON_ERROR(app_param_board_init(), APP_PARAM_BOARD_TAG, "init failed");

    if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        set_err(err_msg, err_msg_size, "flash dump 状态忙");
        return ESP_ERR_TIMEOUT;
    }

    if (op_id != 0 && (s_status.id != op_id || s_status.kind != APP_PARAM_BOARD_OP_KIND_FLASH_DUMP)) {
        xSemaphoreGive(s_state_lock);
        set_err(err_msg, err_msg_size, "flash dump 操作不存在");
        return ESP_ERR_NOT_FOUND;
    }

    status->id = s_status.id;
    status->state = s_status.state;
    snprintf(status->message, sizeof(status->message), "%s", s_status.message);
    status->bytes_read = s_flash_dump_bytes_read;
    status->total_bytes = s_flash_dump_size != 0 ? s_flash_dump_size : APP_PARAM_BOARD_FLASH_DUMP_SIZE;
    status->data_ready = s_flash_dump_ready;

    xSemaphoreGive(s_state_lock);
    return ESP_OK;
}

esp_err_t app_param_board_lock_flash_dump_data(const uint8_t **data,
                                               size_t *size,
                                               char *err_msg,
                                               size_t err_msg_size)
{
    ESP_RETURN_ON_FALSE(data != NULL && size != NULL, ESP_ERR_INVALID_ARG, APP_PARAM_BOARD_TAG, "data arg is null");
    ESP_RETURN_ON_ERROR(app_param_board_init(), APP_PARAM_BOARD_TAG, "init failed");

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        set_err(err_msg, err_msg_size, "参数板卡忙");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        xSemaphoreGive(s_lock);
        set_err(err_msg, err_msg_size, "flash dump 状态忙");
        return ESP_ERR_TIMEOUT;
    }

    if (!s_flash_dump_ready || s_flash_dump_data == NULL || s_flash_dump_size != APP_PARAM_BOARD_FLASH_DUMP_SIZE) {
        xSemaphoreGive(s_state_lock);
        xSemaphoreGive(s_lock);
        set_err(err_msg, err_msg_size, "flash dump 数据尚未准备好");
        return ESP_ERR_INVALID_STATE;
    }

    *data = s_flash_dump_data;
    *size = s_flash_dump_size;
    xSemaphoreGive(s_state_lock);
    return ESP_OK;
}

void app_param_board_unlock_flash_dump_data(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

esp_err_t app_param_board_read(const uint16_t defaults[72], uint16_t values[72], char *err_msg, size_t err_msg_size)
{
    ESP_RETURN_ON_FALSE(values != NULL, ESP_ERR_INVALID_ARG, APP_PARAM_BOARD_TAG, "values is null");
    ESP_RETURN_ON_ERROR(app_param_board_init(), APP_PARAM_BOARD_TAG, "init failed");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        set_err(err_msg, err_msg_size, "参数板卡忙");
        return ESP_ERR_TIMEOUT;
    }

    param_uart_frame_t send = {0};
    param_uart_frame_t recv = {0};
    uint32_t store_addr = 0;
    int ret = board_connect(&send, &recv, 0);
    if (ret != 0) {
        set_err(err_msg, err_msg_size, ret == -2 ? "已取消连接参数板卡" : "连接参数板卡失败");
        xSemaphoreGive(s_lock);
        return ret == -2 ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
    }

    ret = get_param_store_addr(&send, &recv, &store_addr);
    if (ret == 1) {
        if (defaults == NULL) {
            set_err(err_msg, err_msg_size, "板卡参数区未初始化");
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_STATE;
        }
        ret = init_param_store_addr(&send, &recv, &store_addr);
        if (ret == 0) {
            ret = write_params_at(&send, &recv, store_addr, defaults);
        }
    }
    if (ret != 0) {
        set_err(err_msg, err_msg_size, "读取参数存储地址失败");
        xSemaphoreGive(s_lock);
        return ESP_FAIL;
    }

    ret = read_params_at(&send, &recv, store_addr, values);
    xSemaphoreGive(s_lock);
    if (ret != 0) {
        set_err(err_msg, err_msg_size, "参数回读失败");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t app_param_board_write(const uint16_t values[72], char *err_msg, size_t err_msg_size)
{
    ESP_RETURN_ON_FALSE(values != NULL, ESP_ERR_INVALID_ARG, APP_PARAM_BOARD_TAG, "values is null");
    ESP_RETURN_ON_ERROR(app_param_board_init(), APP_PARAM_BOARD_TAG, "init failed");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        set_err(err_msg, err_msg_size, "参数板卡忙");
        return ESP_ERR_TIMEOUT;
    }

    param_uart_frame_t send = {0};
    param_uart_frame_t recv = {0};
    uint32_t store_addr = 0;
    int ret = board_connect(&send, &recv, 0);
    if (ret != 0) {
        set_err(err_msg, err_msg_size, ret == -2 ? "已取消连接参数板卡" : "连接参数板卡失败");
        xSemaphoreGive(s_lock);
        return ret == -2 ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
    }

    ret = get_param_store_addr(&send, &recv, &store_addr);
    if (ret == 1) {
        ret = init_param_store_addr(&send, &recv, &store_addr);
    }
    if (ret != 0) {
        set_err(err_msg, err_msg_size, "获取参数存储地址失败");
        xSemaphoreGive(s_lock);
        return ESP_FAIL;
    }

    ret = write_params_at(&send, &recv, store_addr, values);
    xSemaphoreGive(s_lock);
    if (ret != 0) {
        set_err(err_msg, err_msg_size, "参数下载失败");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_param_board_cancel_connect(void)
{
    if (app_param_board_init() != ESP_OK || s_cmd_queue == NULL || s_state_lock == NULL) {
        return;
    }

    param_board_cmd_t cmd = {
        .type = PARAM_BOARD_CMD_CANCEL,
        .id = 0,
    };

    if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (op_state_is_running(s_status.state)) {
            cmd.id = s_status.id;
        }
        xSemaphoreGive(s_state_lock);
    }

    (void)xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(10));
}
