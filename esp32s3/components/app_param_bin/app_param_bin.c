#include "app_param_bin.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "mbedtls/cipher.h"
#include "mbedtls/gcm.h"

#define PARAM_BIN_FILE_MAGIC "UEPB"
#define PARAM_BIN_FILE_MAGIC_LEN 4
#define PARAM_BIN_FORMAT_VERSION 1
#define PARAM_BIN_HEADER_SIZE 17
#define PARAM_BIN_KEY_LEN 32
#define PARAM_BIN_NONCE_LEN 12
#define PARAM_BIN_TAG_LEN 16
#define PARAM_BIN_MAX_FILE_SIZE (128 * 1024)

#define PARAM_BIN_PAYLOAD_MAGIC "UPLD"
#define PARAM_BIN_PAYLOAD_HEADER_LEN 20
#define PARAM_BIN_RECORD_SIZE 12
#define PARAM_BIN_SCHEMA_VERSION 2
#define PARAM_BIN_ADDR_MAX 71

static const char *TAG = "param_bin";

/* Must be exactly the same as tools/src-tauri/src/crypto.rs PRODUCT_KEY. */
static const uint8_t s_product_key[PARAM_BIN_KEY_LEN] = {
    0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB, 0xED, 0x0F,
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    0x0F, 0xED, 0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21,
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
};

static void set_err(char *err_msg, size_t err_msg_size, const char *msg)
{
    if (err_msg == NULL || err_msg_size == 0) {
        return;
    }
    snprintf(err_msg, err_msg_size, "%s", msg != NULL ? msg : "未知错误");
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void hex_encode_12(const uint8_t *bytes, char out[APP_PARAM_BIN_NONCE_HEX_LEN + 1])
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < PARAM_BIN_NONCE_LEN; i++) {
        out[i * 2] = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[APP_PARAM_BIN_NONCE_HEX_LEN] = '\0';
}

const char *app_param_bin_type_name(app_param_bin_type_t type)
{
    return type == APP_PARAM_BIN_TYPE_PROTECTION ? "protection" : "control";
}

const char *app_param_bin_type_label(app_param_bin_type_t type)
{
    return type == APP_PARAM_BIN_TYPE_PROTECTION ? "保护参数" : "控制参数";
}

const char *app_param_bin_permission_name(app_param_bin_permission_t permission)
{
    return permission == APP_PARAM_BIN_PERMISSION_VISIBLE ? "visible" : "hidden";
}

const char *app_param_bin_permission_label(app_param_bin_permission_t permission)
{
    return permission == APP_PARAM_BIN_PERMISSION_VISIBLE ? "可见" : "隐藏";
}

static esp_err_t read_whole_file(const char *path, uint8_t **out_data, size_t *out_len,
                                 char *err_msg, size_t err_msg_size)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        set_err(err_msg, err_msg_size, "文件不存在");
        return ESP_ERR_NOT_FOUND;
    }
    if (S_ISDIR(st.st_mode)) {
        set_err(err_msg, err_msg_size, "目录不能解析");
        return ESP_ERR_INVALID_ARG;
    }
    if (st.st_size < PARAM_BIN_HEADER_SIZE + PARAM_BIN_TAG_LEN) {
        set_err(err_msg, err_msg_size, "bin 文件过小");
        return ESP_ERR_INVALID_SIZE;
    }
    if (st.st_size > PARAM_BIN_MAX_FILE_SIZE) {
        set_err(err_msg, err_msg_size, "bin 文件超过 ESP32 解析限制");
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "open failed: %s errno=%d", path, errno);
        set_err(err_msg, err_msg_size, "无法打开文件");
        return ESP_FAIL;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)st.st_size);
    if (data == NULL) {
        fclose(f);
        set_err(err_msg, err_msg_size, "内存不足");
        return ESP_ERR_NO_MEM;
    }

    size_t n = fread(data, 1, (size_t)st.st_size, f);
    fclose(f);
    if (n != (size_t)st.st_size) {
        free(data);
        set_err(err_msg, err_msg_size, "读取文件失败");
        return ESP_FAIL;
    }

    *out_data = data;
    *out_len = n;
    return ESP_OK;
}

static esp_err_t aes_gcm_decrypt_payload(const uint8_t *file_data, size_t file_len,
                                         uint8_t **out_plain, size_t *out_plain_len,
                                         char *err_msg, size_t err_msg_size)
{
    if (memcmp(file_data, PARAM_BIN_FILE_MAGIC, PARAM_BIN_FILE_MAGIC_LEN) != 0) {
        set_err(err_msg, err_msg_size, "文件头 magic 不是 UEPB");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (file_data[4] != PARAM_BIN_FORMAT_VERSION) {
        set_err(err_msg, err_msg_size, "bin 格式版本不支持");
        return ESP_ERR_NOT_SUPPORTED;
    }

    const uint8_t *header = file_data;
    const uint8_t *nonce = file_data + 5;
    size_t body_len = file_len - PARAM_BIN_HEADER_SIZE;
    if (body_len < PARAM_BIN_TAG_LEN) {
        set_err(err_msg, err_msg_size, "bin 文件缺少 GCM tag");
        return ESP_ERR_INVALID_SIZE;
    }

    size_t cipher_len = body_len - PARAM_BIN_TAG_LEN;
    const uint8_t *cipher = file_data + PARAM_BIN_HEADER_SIZE;
    const uint8_t *tag = cipher + cipher_len;

    uint8_t *plain = (uint8_t *)malloc(cipher_len);
    if (plain == NULL) {
        set_err(err_msg, err_msg_size, "内存不足");
        return ESP_ERR_NO_MEM;
    }

    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, s_product_key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&ctx,
                                      cipher_len,
                                      nonce,
                                      PARAM_BIN_NONCE_LEN,
                                      header,
                                      PARAM_BIN_HEADER_SIZE,
                                      tag,
                                      PARAM_BIN_TAG_LEN,
                                      cipher,
                                      plain);
    }
    mbedtls_gcm_free(&ctx);

    if (rc != 0) {
        ESP_LOGW(TAG, "AES-GCM decrypt/auth failed: -0x%04x", -rc);
        free(plain);
        set_err(err_msg, err_msg_size, "AES-GCM 解密或认证失败，请确认密钥和文件未被篡改");
        return ESP_ERR_INVALID_CRC;
    }

    *out_plain = plain;
    *out_plain_len = cipher_len;
    return ESP_OK;
}

static esp_err_t decode_payload(const uint8_t *payload, size_t payload_len,
                                app_param_bin_result_t *out,
                                char *err_msg, size_t err_msg_size)
{
    if (payload_len < PARAM_BIN_PAYLOAD_HEADER_LEN) {
        set_err(err_msg, err_msg_size, "Payload 太短");
        return ESP_ERR_INVALID_SIZE;
    }
    if (memcmp(payload, PARAM_BIN_PAYLOAD_MAGIC, 4) != 0) {
        set_err(err_msg, err_msg_size, "Payload magic 不是 UPLD");
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint16_t schema_version = read_le16(payload + 4);
    uint8_t param_count = payload[6];
    uint8_t record_size = payload[7];
    uint16_t board_name_len = read_le16(payload + 8);
    uint16_t name_table_len = read_le16(payload + 10);

    if (schema_version != PARAM_BIN_SCHEMA_VERSION) {
        set_err(err_msg, err_msg_size, "Payload schema 版本不支持");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (param_count != APP_PARAM_BIN_PARAM_COUNT) {
        set_err(err_msg, err_msg_size, "参数数量不是 72");
        return ESP_ERR_INVALID_SIZE;
    }
    if (record_size != PARAM_BIN_RECORD_SIZE) {
        set_err(err_msg, err_msg_size, "参数记录长度不是 12 字节");
        return ESP_ERR_INVALID_SIZE;
    }
    if (board_name_len == 0 || board_name_len > APP_PARAM_BIN_BOARD_NAME_MAX_BYTES) {
        set_err(err_msg, err_msg_size, "板卡名称为空或超过 ESP32 显示限制");
        return ESP_ERR_INVALID_SIZE;
    }

    size_t records_total = (size_t)param_count * record_size;
    size_t board_name_offset = PARAM_BIN_PAYLOAD_HEADER_LEN + records_total;
    size_t name_table_offset = board_name_offset + board_name_len;
    size_t expected_total = name_table_offset + name_table_len;
    if (payload_len < expected_total) {
        set_err(err_msg, err_msg_size, "Payload 板卡名称或名称表不完整");
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(out->board_name, payload + board_name_offset, board_name_len);
    out->board_name[board_name_len] = '\0';

    bool seen[APP_PARAM_BIN_PARAM_COUNT] = {0};
    const uint8_t *records = payload + PARAM_BIN_PAYLOAD_HEADER_LEN;
    const uint8_t *name_table = payload + name_table_offset;

    for (size_t i = 0; i < APP_PARAM_BIN_PARAM_COUNT; i++) {
        const uint8_t *r = records + i * PARAM_BIN_RECORD_SIZE;
        uint8_t address = r[0];
        uint8_t type = r[1];
        uint8_t permission = r[2];
        uint16_t default_value = read_le16(r + 4);
        uint16_t name_offset = read_le16(r + 6);
        uint16_t name_len = read_le16(r + 8);

        if (address > PARAM_BIN_ADDR_MAX) {
            set_err(err_msg, err_msg_size, "参数地址超出 0~71");
            return ESP_ERR_INVALID_ARG;
        }
        if (seen[address]) {
            set_err(err_msg, err_msg_size, "参数地址重复");
            return ESP_ERR_INVALID_ARG;
        }
        seen[address] = true;
        if (type > APP_PARAM_BIN_TYPE_PROTECTION) {
            set_err(err_msg, err_msg_size, "参数类型无效");
            return ESP_ERR_INVALID_ARG;
        }
        if (!(permission == APP_PARAM_BIN_PERMISSION_HIDDEN || permission == APP_PARAM_BIN_PERMISSION_VISIBLE)) {
            set_err(err_msg, err_msg_size, "参数权限无效");
            return ESP_ERR_INVALID_ARG;
        }
        if ((size_t)name_offset + name_len > name_table_len) {
            set_err(err_msg, err_msg_size, "参数名称越界");
            return ESP_ERR_INVALID_ARG;
        }
        if (name_len > APP_PARAM_BIN_NAME_MAX_BYTES) {
            set_err(err_msg, err_msg_size, "参数名称超过 ESP32 显示限制");
            return ESP_ERR_INVALID_SIZE;
        }

        app_param_bin_parameter_t *p = &out->parameters[i];
        p->address = address;
        p->default_value = default_value;
        p->param_type = (app_param_bin_type_t)type;
        p->permission = (app_param_bin_permission_t)permission;
        memcpy(p->name, name_table + name_offset, name_len);
        p->name[name_len] = '\0';
    }

    return ESP_OK;
}

esp_err_t app_param_bin_parse_file(const char *path,
                                   app_param_bin_result_t *out,
                                   char *err_msg,
                                   size_t err_msg_size)
{
    if (path == NULL || out == NULL) {
        set_err(err_msg, err_msg_size, "参数为空");
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    uint8_t *file_data = NULL;
    size_t file_len = 0;
    esp_err_t ret = read_whole_file(path, &file_data, &file_len, err_msg, err_msg_size);
    if (ret != ESP_OK) {
        return ret;
    }

    out->header_size = PARAM_BIN_HEADER_SIZE;
    out->format_version = file_data[4];
    out->file_size = (uint32_t)file_len;
    out->tag_len = PARAM_BIN_TAG_LEN;
    out->ciphertext_len = (uint32_t)(file_len - PARAM_BIN_HEADER_SIZE - PARAM_BIN_TAG_LEN);
    hex_encode_12(file_data + 5, out->nonce_hex);

    uint8_t *plain = NULL;
    size_t plain_len = 0;
    ret = aes_gcm_decrypt_payload(file_data, file_len, &plain, &plain_len, err_msg, err_msg_size);
    free(file_data);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = decode_payload(plain, plain_len, out, err_msg, err_msg_size);
    free(plain);
    return ret;
}
