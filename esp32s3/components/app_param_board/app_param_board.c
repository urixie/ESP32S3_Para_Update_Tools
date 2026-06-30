#include "app_param_board.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define PARAM_BOARD_UART_NUM UART_NUM_1
#define PARAM_BOARD_TX_GPIO GPIO_NUM_11
#define PARAM_BOARD_RX_GPIO GPIO_NUM_12
#define PARAM_BOARD_BAUD_RATE 921600
#define PARAM_BOARD_RX_BUF_SIZE 256
#define PARAM_BOARD_TX_BUF_SIZE 256

#define FLASH_READ_CMD ((uint8_t)0x11)
#define FLASH_WRITE_CMD ((uint8_t)0x22)
#define FLASH_ERASE_CMD ((uint8_t)0x33)
#define FLASH_GET_DNA_CMD ((uint8_t)0x55)

#define PARAM_FRAME_TX_HEAD 0xFA
#define PARAM_FRAME_RX_HEAD 0xFB
#define PARAM_FRAME_TX_END 0xAF
#define PARAM_FRAME_RX_END 0xBF
#define PARAM_FLASH_RETRIES 3
#define PARAM_BOARD_QUEUE_LEN 8
#define PARAM_BOARD_TASK_STACK 4096
#define PARAM_BOARD_TASK_PRIORITY 5

typedef enum {
    PARAM_BOARD_CMD_READ = 0,
    PARAM_BOARD_CMD_WRITE,
    PARAM_BOARD_CMD_CANCEL,
} param_board_cmd_type_t;

typedef struct {
    param_board_cmd_type_t type;
    uint32_t id;
    uint16_t values[APP_PARAM_BOARD_PARAM_COUNT];
} param_board_cmd_t;

typedef struct {
    uint8_t frame_head;
    uint8_t cmd;
    uint8_t addr[3];
    uint8_t data[72];
    uint8_t data_crc[2];
    uint8_t cmd_crc[2];
    uint8_t frame_end;
} __attribute__((packed)) param_uart_frame_t;

static const char *TAG = "param_board";
static bool s_uart_ready;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_state_lock;
static QueueHandle_t s_cmd_queue;
static TaskHandle_t s_task_handle;
static uint32_t s_next_op_id = 1;
static app_param_board_status_t s_status = {
    .id = 0,
    .state = APP_PARAM_BOARD_OP_IDLE,
    .kind = APP_PARAM_BOARD_OP_KIND_NONE,
};

static const uint32_t s_param_store_addr_table[32] = {
    0x30000, 0x31000, 0x32000, 0x33000,
    0x34000, 0x35000, 0x36000, 0x37000,
    0x38000, 0x39000, 0x3A000, 0x3B000,
    0x3C000, 0x3D000, 0x3E000, 0x3F000,
    0x70000, 0x71000, 0x72000, 0x73000,
    0x74000, 0x75000, 0x76000, 0x77000,
    0x78000, 0x79000, 0x7A000, 0x7B000,
    0x7C000, 0x7D000, 0x7E000, 0x7F000,
};

static void app_param_board_task(void *arg);

static void set_err(char *err_msg, size_t err_msg_size, const char *msg)
{
    if (err_msg != NULL && err_msg_size > 0) {
        snprintf(err_msg, err_msg_size, "%s", msg != NULL ? msg : "参数板卡操作失败");
    }
}

static bool op_state_is_running(app_param_board_op_state_t state)
{
    return state == APP_PARAM_BOARD_OP_RUNNING;
}

static void status_set(uint32_t id,
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

static void status_message(uint32_t id, const char *message)
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

static bool param_board_recv_cancel(uint32_t op_id)
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
            ESP_LOGW(TAG, "drop command while parameter operation is busy: type=%d id=%lu",
                     (int)cmd.type, (unsigned long)cmd.id);
        }
    }
    return canceled;
}

static uint16_t crc16_modbus(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static void frame_prepare(param_uart_frame_t *frame, uint8_t cmd, uint32_t sector_addr)
{
    uint16_t crc;
    frame->frame_head = PARAM_FRAME_TX_HEAD;
    frame->cmd = cmd;
    frame->addr[0] = (uint8_t)((sector_addr >> 16) & 0xFF);
    frame->addr[1] = (uint8_t)((sector_addr >> 8) & 0xFF);
    frame->addr[2] = (uint8_t)(sector_addr & 0xFF);

    crc = crc16_modbus(frame->data, sizeof(frame->data));
    frame->data_crc[0] = (uint8_t)(crc >> 8);
    frame->data_crc[1] = (uint8_t)crc;

    crc = crc16_modbus(&frame->cmd, sizeof(frame->data) + 6);
    frame->cmd_crc[0] = (uint8_t)(crc >> 8);
    frame->cmd_crc[1] = (uint8_t)crc;
    frame->frame_end = PARAM_FRAME_TX_END;
}

static int frame_recv(param_uart_frame_t *frame, uint8_t operation, TickType_t timeout)
{
    int bytes_read = uart_read_bytes(PARAM_BOARD_UART_NUM, (uint8_t *)frame, sizeof(*frame), timeout);
    if (bytes_read != sizeof(*frame)) {
        return -1;
    }
    if (frame->frame_head != PARAM_FRAME_RX_HEAD) {
        return -2;
    }

    bool data_all_ff = true;
    for (size_t i = 0; i < sizeof(frame->data); i++) {
        if (frame->data[i] != 0xFF) {
            data_all_ff = false;
            break;
        }
    }
    bool crc_all_ff = (frame->data_crc[0] == 0xFF && frame->data_crc[1] == 0xFF);
    if (data_all_ff && crc_all_ff) {
        if (operation == FLASH_READ_CMD) {
            return 1;
        }
    } else {
        uint16_t crc = crc16_modbus(frame->data, sizeof(frame->data));
        if ((uint8_t)(crc >> 8) != frame->data_crc[0] || (uint8_t)crc != frame->data_crc[1]) {
            if (operation == FLASH_READ_CMD || operation == FLASH_WRITE_CMD) {
                ESP_LOGE(TAG, "data crc mismatch calc=%04X recv=%02X%02X",
                         crc, frame->data_crc[0], frame->data_crc[1]);
                return -3;
            }
        }
    }

    uint16_t cmd_crc = crc16_modbus(&frame->cmd, sizeof(frame->data) + 6);
    if ((uint8_t)(cmd_crc >> 8) != frame->cmd_crc[0] || (uint8_t)cmd_crc != frame->cmd_crc[1]) {
        ESP_LOGE(TAG, "cmd crc mismatch calc=%04X recv=%02X%02X",
                 cmd_crc, frame->cmd_crc[0], frame->cmd_crc[1]);
        return -4;
    }
    if (frame->frame_end != PARAM_FRAME_RX_END) {
        return -5;
    }
    return 0;
}

static int flash_operate(param_uart_frame_t *send, param_uart_frame_t *recv, uint8_t operation, uint32_t sector_addr)
{
    int ret = 0;
    frame_prepare(send, operation, sector_addr);
    for (int retry = 0; retry < PARAM_FLASH_RETRIES; retry++) {
        memset(recv, 0, sizeof(*recv));
        uart_flush_input(PARAM_BOARD_UART_NUM);
        uart_write_bytes(PARAM_BOARD_UART_NUM, (const char *)send, sizeof(*send));
        vTaskDelay(pdMS_TO_TICKS(operation == FLASH_ERASE_CMD ? 65 : 10));
        ret = frame_recv(recv, operation, pdMS_TO_TICKS(8));
        if (ret != -1) {
            break;
        }
    }

    if (ret == 0 && operation == FLASH_WRITE_CMD &&
        memcmp(send->data, recv->data, sizeof(send->data)) != 0) {
        ret = -6;
    }
    return ret;
}

static void copy_frame_to_params(uint16_t *dest, const uint8_t *src, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        dest[i] = ((uint16_t)src[i * 2] << 8) | src[i * 2 + 1];
    }
}

static void copy_params_to_frame(uint8_t *dest, const uint16_t *src, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        dest[i * 2] = (uint8_t)(src[i] >> 8);
        dest[i * 2 + 1] = (uint8_t)src[i];
    }
}

static bool store_addr_valid(uint32_t addr)
{
    for (size_t i = 0; i < sizeof(s_param_store_addr_table) / sizeof(s_param_store_addr_table[0]); i++) {
        if (addr == s_param_store_addr_table[i]) {
            return true;
        }
    }
    return false;
}

static int board_connect(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t op_id)
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
        uart_flush_input(PARAM_BOARD_UART_NUM);
        uart_write_bytes(PARAM_BOARD_UART_NUM, (const char *)send, sizeof(*send));
        vTaskDelay(pdMS_TO_TICKS(10));
        int ret = frame_recv(recv, FLASH_GET_DNA_CMD, pdMS_TO_TICKS(8));
        if (ret == 0) {
            ESP_LOGI(TAG, "board handshake ok");
            return 0;
        }
    }
    return -1;
}

static int get_param_store_addr(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t *store_addr)
{
    memset(send, 0, sizeof(*send));
    int ret = flash_operate(send, recv, FLASH_READ_CMD, s_param_store_addr_table[0]);
    if (ret != 0) {
        return ret;
    }

    uint32_t addr = ((uint32_t)recv->data[0] << 16) | ((uint32_t)recv->data[1] << 8) | recv->data[2];
    if (!store_addr_valid(addr)) {
        return 1;
    }
    *store_addr = addr;
    return 0;
}

static int init_param_store_addr(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t *store_addr)
{
    memset(send, 0, sizeof(*send));
    int ret = flash_operate(send, recv, FLASH_ERASE_CMD, s_param_store_addr_table[0]);
    if (ret != 0) {
        return -1;
    }

    memset(send, 0, sizeof(*send));
    uint32_t addr = s_param_store_addr_table[1];
    send->data[0] = (uint8_t)((addr >> 16) & 0xFF);
    send->data[1] = (uint8_t)((addr >> 8) & 0xFF);
    send->data[2] = (uint8_t)(addr & 0xFF);
    ret = flash_operate(send, recv, FLASH_WRITE_CMD, s_param_store_addr_table[0]);
    if (ret != 0) {
        return -2;
    }
    *store_addr = addr;
    return 0;
}

static int read_params_at(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t store_addr, uint16_t values[72])
{
    for (int block = 0; block < 2; block++) {
        memset(send, 0, sizeof(*send));
        uint32_t addr = store_addr + (uint32_t)block * 74U;
        int ret = flash_operate(send, recv, FLASH_READ_CMD, addr);
        if (ret != 0) {
            return ret;
        }
        copy_frame_to_params(values + block * 36, recv->data, 36);
    }
    return 0;
}

static int write_params_at(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t store_addr, const uint16_t values[72])
{
    memset(send, 0, sizeof(*send));
    int ret = flash_operate(send, recv, FLASH_ERASE_CMD, store_addr);
    if (ret != 0) {
        return -2;
    }

    for (int block = 0; block < 2; block++) {
        memset(send, 0, sizeof(*send));
        copy_params_to_frame(send->data, values + block * 36, 36);
        uint32_t addr = store_addr + (uint32_t)block * 74U;
        ret = flash_operate(send, recv, FLASH_WRITE_CMD, addr);
        if (ret != 0) {
            return -3;
        }
    }
    return 0;
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
    }

    xSemaphoreGive(s_lock);

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

static void app_param_board_task(void *arg)
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

esp_err_t app_param_board_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "create mutex failed");
    }
    if (s_state_lock == NULL) {
        s_state_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_state_lock != NULL, ESP_ERR_NO_MEM, TAG, "create state mutex failed");
    }
    if (s_cmd_queue == NULL) {
        s_cmd_queue = xQueueCreate(PARAM_BOARD_QUEUE_LEN, sizeof(param_board_cmd_t));
        ESP_RETURN_ON_FALSE(s_cmd_queue != NULL, ESP_ERR_NO_MEM, TAG, "create command queue failed");
    }
    if (s_task_handle == NULL) {
        BaseType_t ok = xTaskCreate(app_param_board_task,
                                    "param_board_task",
                                    PARAM_BOARD_TASK_STACK,
                                    NULL,
                                    PARAM_BOARD_TASK_PRIORITY,
                                    &s_task_handle);
        ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "create parameter task failed");
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
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_param_config(PARAM_BOARD_UART_NUM, &uart_config), TAG, "uart config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(PARAM_BOARD_UART_NUM, PARAM_BOARD_TX_GPIO, PARAM_BOARD_RX_GPIO,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "uart pin config failed");
    ESP_RETURN_ON_ERROR(uart_driver_install(PARAM_BOARD_UART_NUM, PARAM_BOARD_RX_BUF_SIZE,
                                            PARAM_BOARD_TX_BUF_SIZE, 0, NULL, 0),
                        TAG, "uart driver install failed");
    s_uart_ready = true;
    ESP_LOGI(TAG, "UART%d ready: tx=%d rx=%d baud=%d",
             PARAM_BOARD_UART_NUM, PARAM_BOARD_TX_GPIO, PARAM_BOARD_RX_GPIO, PARAM_BOARD_BAUD_RATE);
    return ESP_OK;
}

static esp_err_t start_operation(param_board_cmd_type_t type,
                                 const uint16_t values[APP_PARAM_BOARD_PARAM_COUNT],
                                 uint32_t *op_id,
                                 char *err_msg,
                                 size_t err_msg_size)
{
    ESP_RETURN_ON_FALSE(values != NULL, ESP_ERR_INVALID_ARG, TAG, "values is null");
    ESP_RETURN_ON_FALSE(type == PARAM_BOARD_CMD_READ || type == PARAM_BOARD_CMD_WRITE,
                        ESP_ERR_INVALID_ARG, TAG, "invalid command type");
    ESP_RETURN_ON_ERROR(app_param_board_init(), TAG, "init failed");

    param_board_cmd_t cmd = {
        .type = type,
    };
    memcpy(cmd.values, values, sizeof(cmd.values));

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
    s_status.kind = type == PARAM_BOARD_CMD_READ ? APP_PARAM_BOARD_OP_KIND_READ : APP_PARAM_BOARD_OP_KIND_WRITE;
    s_status.state = APP_PARAM_BOARD_OP_RUNNING;
    s_status.has_values = false;
    snprintf(s_status.message, sizeof(s_status.message), "%s",
             type == PARAM_BOARD_CMD_READ ? "等待参数回读任务执行" : "等待参数下载任务执行");
    xSemaphoreGive(s_state_lock);

    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        status_set(cmd.id,
                   type == PARAM_BOARD_CMD_READ ? APP_PARAM_BOARD_OP_KIND_READ : APP_PARAM_BOARD_OP_KIND_WRITE,
                   APP_PARAM_BOARD_OP_FAILED,
                   "参数任务队列忙",
                   NULL);
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

esp_err_t app_param_board_get_status(uint32_t op_id,
                                     app_param_board_status_t *status,
                                     char *err_msg,
                                     size_t err_msg_size)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is null");
    ESP_RETURN_ON_ERROR(app_param_board_init(), TAG, "init failed");

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

esp_err_t app_param_board_read(const uint16_t defaults[72], uint16_t values[72], char *err_msg, size_t err_msg_size)
{
    ESP_RETURN_ON_FALSE(values != NULL, ESP_ERR_INVALID_ARG, TAG, "values is null");
    ESP_RETURN_ON_ERROR(app_param_board_init(), TAG, "init failed");
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
    ESP_RETURN_ON_FALSE(values != NULL, ESP_ERR_INVALID_ARG, TAG, "values is null");
    ESP_RETURN_ON_ERROR(app_param_board_init(), TAG, "init failed");
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
