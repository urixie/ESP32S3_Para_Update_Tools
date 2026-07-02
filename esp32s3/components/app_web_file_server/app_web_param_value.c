#include "app_web_param_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_web_common.h"

static esp_err_t param_ns_to_board_tick(uint32_t value_ns,
                                        uint16_t *out_tick,
                                        char *err_msg,
                                        size_t err_msg_size)
{
    if (value_ns > APP_WEB_PARAM_MAX_NS_VALUE) {
        snprintf(err_msg, err_msg_size, "参数值必须在 0~%lu ns",
                 (unsigned long)APP_WEB_PARAM_MAX_NS_VALUE);
        return ESP_ERR_INVALID_ARG;
    }
    if ((value_ns % APP_WEB_PARAM_TIME_BASE_NS) != 0) {
        snprintf(err_msg, err_msg_size, "参数值必须是 %u ns 的整数倍",
                 (unsigned int)APP_WEB_PARAM_TIME_BASE_NS);
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t tick = value_ns / APP_WEB_PARAM_TIME_BASE_NS;
    if (tick > UINT16_MAX) {
        snprintf(err_msg, err_msg_size, "参数值超过板卡存储范围");
        return ESP_ERR_INVALID_ARG;
    }

    *out_tick = (uint16_t)tick;
    return ESP_OK;
}

esp_err_t param_ns_values_to_board_ticks(const uint32_t values_ns[APP_PARAM_BIN_PARAM_COUNT],
                                                uint16_t board_values[APP_PARAM_BIN_PARAM_COUNT],
                                                char *err_msg,
                                                size_t err_msg_size)
{
    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        esp_err_t ret = param_ns_to_board_tick(values_ns[i], &board_values[i], err_msg, err_msg_size);
        if (ret != ESP_OK) {
            char detail[160];
            snprintf(detail, sizeof(detail), "参数地址 %u：%s", (unsigned int)i,
                     err_msg != NULL && err_msg[0] != '\0' ? err_msg : "参数值无效");
            snprintf(err_msg, err_msg_size, "%s", detail);
            return ret;
        }
    }
    return ESP_OK;
}

uint32_t param_board_tick_to_ns(uint16_t value)
{
    return (uint32_t)value * APP_WEB_PARAM_TIME_BASE_NS;
}

void build_param_defaults_ns(const app_param_bin_result_t *parsed, uint32_t defaults_ns[APP_PARAM_BIN_PARAM_COUNT])
{
    memset(defaults_ns, 0, sizeof(uint32_t) * APP_PARAM_BIN_PARAM_COUNT);
    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const app_param_bin_parameter_t *p = &parsed->parameters[i];
        if (p->address < APP_PARAM_BIN_PARAM_COUNT) {
            defaults_ns[p->address] = p->default_value;
        }
    }
}

static bool param_address_is_visible(const app_param_bin_result_t *parsed, uint8_t address)
{
    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const app_param_bin_parameter_t *p = &parsed->parameters[i];
        if (p->address == address) {
            return p->permission == APP_PARAM_BIN_PERMISSION_VISIBLE;
        }
    }
    return false;
}

esp_err_t parse_param_values(const char *text,
                                    const app_param_bin_result_t *parsed,
                                    bool provided[APP_PARAM_BIN_PARAM_COUNT],
                                    uint32_t values_ns[APP_PARAM_BIN_PARAM_COUNT],
                                    char *err_msg,
                                    size_t err_msg_size)
{
    if (text == NULL || text[0] == '\0') {
        snprintf(err_msg, err_msg_size, "缺少参数值");
        return ESP_ERR_INVALID_ARG;
    }

    char buf[1024];
    int len = snprintf(buf, sizeof(buf), "%s", text);
    if (len <= 0 || (size_t)len >= sizeof(buf)) {
        snprintf(err_msg, err_msg_size, "参数值过长");
        return ESP_ERR_INVALID_SIZE;
    }

    char *saveptr = NULL;
    char *token = strtok_r(buf, ",", &saveptr);
    while (token != NULL) {
        char *sep = strchr(token, ':');
        if (sep == NULL) {
            snprintf(err_msg, err_msg_size, "参数格式错误");
            return ESP_ERR_INVALID_ARG;
        }
        *sep = '\0';
        char *end = NULL;
        unsigned long addr = strtoul(token, &end, 10);
        if (end == token || *end != '\0' || addr >= APP_PARAM_BIN_PARAM_COUNT) {
            snprintf(err_msg, err_msg_size, "参数地址无效");
            return ESP_ERR_INVALID_ARG;
        }
        unsigned long value = strtoul(sep + 1, &end, 10);
        if (end == sep + 1 || *end != '\0' || value > APP_WEB_PARAM_MAX_NS_VALUE) {
            snprintf(err_msg, err_msg_size, "参数值无效");
            return ESP_ERR_INVALID_ARG;
        }
        uint16_t unused_tick = 0;
        esp_err_t ret = param_ns_to_board_tick((uint32_t)value, &unused_tick, err_msg, err_msg_size);
        if (ret != ESP_OK) {
            return ret;
        }
        if (!param_address_is_visible(parsed, (uint8_t)addr)) {
            snprintf(err_msg, err_msg_size, "参数地址未授权");
            return ESP_ERR_INVALID_ARG;
        }
        provided[addr] = true;
        values_ns[addr] = (uint32_t)value;
        token = strtok_r(NULL, ",", &saveptr);
    }

    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const app_param_bin_parameter_t *p = &parsed->parameters[i];
        if (p->permission == APP_PARAM_BIN_PERMISSION_VISIBLE && !provided[p->address]) {
            snprintf(err_msg, err_msg_size, "存在未填写的可见参数");
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}
