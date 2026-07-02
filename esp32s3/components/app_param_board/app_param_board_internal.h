#ifndef APP_PARAM_BOARD_INTERNAL_H
#define APP_PARAM_BOARD_INTERNAL_H

#include "app_param_board.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PARAM_BOARD_TAG "param_board"

#define PARAM_BOARD_UART_NUM UART_NUM_1
#define PARAM_BOARD_TX_GPIO GPIO_NUM_11
#define PARAM_BOARD_RX_GPIO GPIO_NUM_12
#define PARAM_BOARD_BAUD_RATE 921600
#define PARAM_BOARD_RX_BUF_SIZE 256
#define PARAM_BOARD_TX_BUF_SIZE 256
#define PARAM_BOARD_UART_EVENT_QUEUE_SIZE 6

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
#define PARAM_FLASH_DUMP_FRAME_BYTES 74U

#ifndef APP_PARAM_BOARD_RAW_DUMP
#define APP_PARAM_BOARD_RAW_DUMP 0
#endif

typedef enum {
    PARAM_BOARD_CMD_READ = 0,
    PARAM_BOARD_CMD_WRITE,
    PARAM_BOARD_CMD_FLASH_DUMP,
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

extern SemaphoreHandle_t s_lock;
extern SemaphoreHandle_t s_state_lock;
extern QueueHandle_t s_cmd_queue;
extern uint8_t *s_flash_dump_data;
extern size_t s_flash_dump_bytes_read;
extern size_t s_flash_dump_size;
extern bool s_flash_dump_ready;
extern app_param_board_status_t s_status;

void set_err(char *err_msg, size_t err_msg_size, const char *msg);
bool op_state_is_running(app_param_board_op_state_t state);
app_param_board_op_kind_t cmd_kind(param_board_cmd_type_t type);
const char *cmd_wait_message(param_board_cmd_type_t type);
void status_set(uint32_t id,
                app_param_board_op_kind_t kind,
                app_param_board_op_state_t state,
                const char *message,
                const uint16_t *values);
void status_message(uint32_t id, const char *message);
void flash_dump_status_update(uint32_t id, size_t bytes_read, const char *message);
void flash_dump_status_finish(uint32_t id,
                              app_param_board_op_state_t state,
                              const char *message,
                              size_t bytes_read,
                              bool data_ready);
bool param_board_recv_cancel(uint32_t op_id);

void frame_prepare(param_uart_frame_t *frame, uint8_t cmd, uint32_t sector_addr);
int frame_recv(param_uart_frame_t *frame, uint8_t operation, TickType_t timeout);
int flash_dump_read_packet(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t sector_addr);
int flash_operate(param_uart_frame_t *send, param_uart_frame_t *recv, uint8_t operation, uint32_t sector_addr);

int board_connect(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t op_id);
int get_param_store_addr(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t *store_addr);
int init_param_store_addr(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t *store_addr);
int read_params_at(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t store_addr, uint16_t values[72]);
int write_params_at(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t store_addr, const uint16_t values[72]);

void app_param_board_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif
