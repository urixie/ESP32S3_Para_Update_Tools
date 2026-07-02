#include "app_param_board_internal.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

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

#if APP_PARAM_BOARD_RAW_DUMP
static const char *operation_name(uint8_t operation)
{
    switch (operation) {
    case FLASH_READ_CMD:
        return "read";
    case FLASH_WRITE_CMD:
        return "write";
    case FLASH_ERASE_CMD:
        return "erase";
    case FLASH_GET_DNA_CMD:
        return "handshake";
    default:
        return "unknown";
    }
}

static void dump_raw_frame(const char *dir,
                           const param_uart_frame_t *frame,
                           uint8_t operation,
                           uint32_t sector_addr,
                           int retry,
                           int ret)
{
    ESP_LOGI(APP_PARAM_BOARD_TAG,
             "%s raw frame op=%s(0x%02X) addr=0x%06lX retry=%d ret=%d len=%u",
             dir,
             operation_name(operation),
             operation,
             (unsigned long)sector_addr,
             retry,
             ret,
             (unsigned int)sizeof(*frame));
    ESP_LOG_BUFFER_HEX_LEVEL(APP_PARAM_BOARD_TAG, (const uint8_t *)frame, sizeof(*frame), ESP_LOG_INFO);
}
#endif

#if APP_PARAM_BOARD_RAW_DUMP
#define PARAM_BOARD_DUMP_RAW(dir, frame, operation, sector_addr, retry, ret) \
    dump_raw_frame((dir), (frame), (operation), (sector_addr), (retry), (ret))
#else
#define PARAM_BOARD_DUMP_RAW(dir, frame, operation, sector_addr, retry, ret) \
    do {                                                                    \
    } while (0)
#endif

void frame_prepare(param_uart_frame_t *frame, uint8_t cmd, uint32_t sector_addr)
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

int frame_recv(param_uart_frame_t *frame, uint8_t operation, TickType_t timeout)
{
    int ret = 0;
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
            ret = 1;
        }
    } else {
        uint16_t crc = crc16_modbus(frame->data, sizeof(frame->data));
        if ((uint8_t)(crc >> 8) != frame->data_crc[0] || (uint8_t)crc != frame->data_crc[1]) {
            if (operation == FLASH_READ_CMD || operation == FLASH_WRITE_CMD) {
                ESP_LOGE(APP_PARAM_BOARD_TAG, "data crc mismatch calc=%04X recv=%02X%02X",
                         crc, frame->data_crc[0], frame->data_crc[1]);
                return -3;
            }
        }
    }

    uint16_t cmd_crc = crc16_modbus(&frame->cmd, sizeof(frame->data) + 6);
    if ((uint8_t)(cmd_crc >> 8) != frame->cmd_crc[0] || (uint8_t)cmd_crc != frame->cmd_crc[1]) {
        ESP_LOGE(APP_PARAM_BOARD_TAG, "cmd crc mismatch calc=%04X recv=%02X%02X",
                 cmd_crc, frame->cmd_crc[0], frame->cmd_crc[1]);
        return -4;
    }
    if (frame->frame_end != PARAM_FRAME_RX_END) {
        return -5;
    }
    return ret;
}

static int frame_recv_flash_dump(param_uart_frame_t *frame, TickType_t timeout)
{
    int bytes_read = uart_read_bytes(PARAM_BOARD_UART_NUM, (uint8_t *)frame, sizeof(*frame), timeout);
    if (bytes_read != sizeof(*frame)) {
        return -1;
    }
    if (frame->frame_head != PARAM_FRAME_RX_HEAD) {
        return -2;
    }

    uint16_t cmd_crc = crc16_modbus(&frame->cmd, sizeof(frame->data) + 6);
    if ((uint8_t)(cmd_crc >> 8) != frame->cmd_crc[0] || (uint8_t)cmd_crc != frame->cmd_crc[1]) {
        ESP_LOGE(APP_PARAM_BOARD_TAG, "flash dump cmd crc mismatch calc=%04X recv=%02X%02X",
                 cmd_crc, frame->cmd_crc[0], frame->cmd_crc[1]);
        return -4;
    }
    if (frame->frame_end != PARAM_FRAME_RX_END) {
        return -5;
    }
    return 0;
}

int flash_dump_read_packet(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t sector_addr)
{
    int ret = 0;
    frame_prepare(send, FLASH_READ_CMD, sector_addr);
    for (int retry = 0; retry < PARAM_FLASH_RETRIES; retry++) {
        memset(recv, 0, sizeof(*recv));
        uart_flush_input(PARAM_BOARD_UART_NUM);
        uart_write_bytes(PARAM_BOARD_UART_NUM, (const char *)send, sizeof(*send));
        vTaskDelay(pdMS_TO_TICKS(10));
        ret = frame_recv_flash_dump(recv, pdMS_TO_TICKS(3));
        if (ret != -1) {
            break;
        }
    }
    return ret;
}

static int flash_operate_ex(param_uart_frame_t *send,
                            param_uart_frame_t *recv,
                            uint8_t operation,
                            uint32_t sector_addr,
                            bool log_raw)
{
    int ret = 0;
    frame_prepare(send, operation, sector_addr);
    for (int retry = 0; retry < PARAM_FLASH_RETRIES; retry++) {
        memset(recv, 0, sizeof(*recv));
        uart_flush_input(PARAM_BOARD_UART_NUM);
        if (log_raw) {
            PARAM_BOARD_DUMP_RAW("tx", send, operation, sector_addr, retry, ret);
        }
        uart_write_bytes(PARAM_BOARD_UART_NUM, (const char *)send, sizeof(*send));
        vTaskDelay(pdMS_TO_TICKS(operation == FLASH_ERASE_CMD ? 65 : 10));
        ret = frame_recv(recv, operation, pdMS_TO_TICKS(3));
        if (ret != -1) {
            if (log_raw) {
                PARAM_BOARD_DUMP_RAW("rx", recv, operation, sector_addr, retry, ret);
            }
        } else {
#if APP_PARAM_BOARD_RAW_DUMP
            if (log_raw) {
                ESP_LOGW(APP_PARAM_BOARD_TAG,
                         "rx raw frame timeout op=%s(0x%02X) addr=0x%06lX retry=%d",
                         operation_name(operation),
                         operation,
                         (unsigned long)sector_addr,
                         retry);
            }
#endif
        }
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

int flash_operate(param_uart_frame_t *send, param_uart_frame_t *recv, uint8_t operation, uint32_t sector_addr)
{
    return flash_operate_ex(send, recv, operation, sector_addr, true);
}
