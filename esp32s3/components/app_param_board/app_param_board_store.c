#include "app_param_board_internal.h"

#include <string.h>

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

int get_param_store_addr(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t *store_addr)
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

int init_param_store_addr(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t *store_addr)
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

int read_params_at(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t store_addr, uint16_t values[72])
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

int write_params_at(param_uart_frame_t *send, param_uart_frame_t *recv, uint32_t store_addr, const uint16_t values[72])
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
