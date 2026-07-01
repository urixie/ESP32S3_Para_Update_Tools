# 驱动器参数配置上位机文件协议 (ESP32 端实现参考)

> 本文档描述由 驱动器参数配置上位机 生成的 `.bin` 文件的二进制格式，供 ESP32
> 端 C 代码解析使用。协议固定、小端、长度固定，适合嵌入式环境直接 memcmp
> + memcpy。

---

## 1. 文件总体结构

```text
+--------------------------+
| Header 明文区，固定48字节 |
+--------------------------+
| AES-GCM ciphertext        |
+--------------------------+
| AES-GCM tag，固定16字节   |
+--------------------------+
```

- **Header**：48 字节明文，**不包含**中文名称、默认值、类型、权限。
- **Payload**：AES-GCM 密文，长度由 Header 里的 `payload_len` 字段给出。
- **Tag**：AES-GCM 16 字节认证 tag，紧跟 ciphertext 之后。

中文名称、默认值、类型、权限全部位于加密的 Payload 内，因此用十六进制
工具打开 bin 文件**看不到**中文明文，也看不到任何敏感字段。

---

## 2. Header 格式 (48 字节，小端)

| 偏移 | 字段           | 类型       | 大小 | 说明                       |
| ---: | -------------- | ---------- | ---: | -------------------------- |
|    0 | magic          | `[u8;4]`   |    4 | 固定 `"UEPB"`              |
|    4 | header_len     | `u16`      |    2 | 固定 `48`                  |
|    6 | format_version | `u16`      |    2 | 固定 `1`                   |
|    8 | crypto_algo    | `u8`       |    1 | 固定 `1`，表示 AES-256-GCM |
|    9 | param_count    | `u8`       |    1 | 固定 `72`                  |
|   10 | addr_min       | `u8`       |    1 | 固定 `0`                   |
|   11 | addr_max       | `u8`       |    1 | 固定 `71`                  |
|   12 | product_id     | `u32`      |    4 | 第一版为 `1`               |
|   16 | key_id         | `u32`      |    4 | 第一版为 `1`               |
|   20 | flags          | `u32`      |    4 | 预留，默认 `0`             |
|   24 | nonce          | `[u8;12]`  |   12 | AES-GCM nonce              |
|   36 | payload_len    | `u32`      |    4 | ciphertext 长度，**不含 tag** |
|   40 | tag_len        | `u8`       |    1 | 固定 `16`                  |
|   41 | reserved       | `[u8;7]`   |    7 | 全部为 `0`                 |

总计 48 字节，全部字段小端。

---

## 3. AES-GCM 说明

| 项目     | 值                                                    |
| -------- | ----------------------------------------------------- |
| 算法     | AES-256-GCM                                           |
| Key      | 32 字节产品级固定密钥，与 ESP32 固件内置相同          |
| Nonce    | Header 偏移 24 的 12 字节                             |
| AAD      | 完整 48 字节 Header（**解密时必须作为 AAD**）         |
| ciphertext | Header 之后的前 `payload_len` 字节                   |
| tag      | ciphertext 之后的 16 字节                             |

要点：

1. 每次导出生成新的随机 12 字节 nonce。
2. ESP32 解密时必须把 48 字节 Header 整体作为 AAD。
3. AES-GCM 输出 `ciphertext || tag`，`payload_len` 仅描述 ciphertext 长度。
4. 任意字节被修改，tag 校验应失败（DecryptFailed）。
5. 密钥永远不出现在 bin 文件中，只通过 `key_id` 引用。

---

## 4. Payload 明文结构

解密成功后的明文 Payload 结构：

```text
+--------------------------+
| Payload Header 16字节     |
+--------------------------+
| Parameter Record[72]     |
+--------------------------+
| Name Table               |
+--------------------------+
```

---

## 5. Payload Header (16 字节，小端)

| 偏移 | 字段            | 类型       | 大小 | 说明                                  |
| ---: | --------------- | ---------- | ---: | ------------------------------------- |
|    0 | payload_magic   | `[u8;4]`   |    4 | 固定 `"UPLD"`                         |
|    4 | schema_version  | `u16`      |    2 | 固定 `1`                              |
|    6 | param_count     | `u8`       |    1 | 固定 `72`                             |
|    7 | record_size     | `u8`       |    1 | 固定 `12`                             |
|    8 | name_table_len  | `u16`      |    2 | 中文名称表长度                        |
|   10 | payload_flags   | `u16`      |    2 | 预留，默认 `0`                        |
|   12 | payload_crc     | `u32`      |    4 | 第一版为 `0`，因为 GCM 已认证完整性  |

---

## 6. Parameter Record (12 字节 × 72)

每个参数记录固定 12 字节：

| 偏移 | 字段           | 类型   | 大小 | 说明                              |
| ---: | -------------- | ------ | ---: | --------------------------------- |
|    0 | address        | `u8`   |    1 | `0~71`                            |
|    1 | param_type     | `u8`   |    1 | `0`=控制, `1`=保护                |
|    2 | permission     | `u8`   |    1 | `0`=隐藏, `1`=可见                |
|    3 | reserved0      | `u8`   |    1 | `0`                               |
|    4 | default_value  | `u16`  |    2 | 默认值                            |
|    6 | name_offset    | `u16`  |    2 | 名称表偏移（相对 Name Table）     |
|    8 | name_len       | `u16`  |    2 | 名称 UTF-8 字节长度               |
|   10 | reserved1      | `u16`  |    2 | `0`                               |

权限编码（必须在 ESP32 端也保持一致）：

```text
0 = 隐藏 (HIDDEN)
1 = 可见 (VISIBLE)
```

---

## 7. Name Table

```text
所有名称使用 UTF-8 编码
不强制 \0 结尾
通过 (record.name_offset + record.name_len) 定位
整个 Name Table 位于加密 Payload 中
```

ESP32 端解析 Name Table 时不要假设有 `\0` 结尾，按 `name_len` 显式读取
对应字节数即可。

---

## 8. ESP32 端解析流程

按以下顺序解析：

```text
1.  读取整个 bin 文件到内存
2.  检查 file_size >= 48 + 16 (= 64)
3.  解析 Header (48 B)
4.  检查 magic == "UEPB"，否则拒绝
5.  检查 header_len == 48
6.  检查 format_version == 1
7.  检查 crypto_algo == 1 (AES-256-GCM)
8.  检查 param_count == 72
9.  检查 addr_min == 0 且 addr_max == 71
10. 根据 product_id / key_id 查表获取 32 字节密钥
11. ciphertext = data[48 .. 48 + payload_len]
12. tag        = data[48 + payload_len .. 48 + payload_len + 16]
13. 使用 AES-256-GCM 解密，AAD = data[0 .. 48]
14. 解密失败（tag 不匹配）则返回 "Bin 文件损坏、被篡改或密钥错误"
15. 解密成功得到 plaintext
16. 解析 Payload Header (16 B)
17. 检查 payload_magic == "UPLD"
18. 检查 schema_version == 1
19. 检查 record_size == 12
20. 解析 72 个 Parameter Record
21. 检查地址范围 0~71 且不能重复
22. 通过 name_offset / name_len 从 Name Table 取 UTF-8 中文名称
23. 构建运行时参数表 g_param_table[72]
24. 清理解密用的临时缓冲
```

> 注意：ESP32 是小端架构，但建议仍使用显式的 little-endian 读取函数
>（`param_read_u16_le`、`param_read_u32_le`），避免不同 SDK / 编译器优化
> 带来的字节序问题。

---

## 9. 隐藏参数处理规则（重要）

ESP32 端必须遵守：

```text
1. ESP32 内部可以保存全部 72 个参数（控制 + 保护）。
2. 控制逻辑、保护逻辑、调试接口可以使用全部 72 个参数。
3. 客户界面/API 只能返回 permission == 1 (可见) 的参数。
4. permission == 0 (隐藏) 的参数不能通过以下任一通道返回：
     - Web / HTTP API
     - 串口命令
     - BLE GATT
     - MQTT 主题
     - 任何对外通信接口
5. 对客户而言，隐藏参数应表现为 "不存在" 或 "PARAM_ERR_NOT_FOUND"。
```

错误示例（**不要这样做**）：

```c
// ❌ 错误：返回所有参数给网页前端，由前端隐藏 Hidden 参数
// 客户可以直接抓包绕过前端
return http_send_all_params(g_param_table);
```

正确示例：

```c
bool param_is_visible(uint8_t address) {
    if (address >= PARAM_COUNT) return false;
    return g_param_table[address].permission == PARAM_PERMISSION_VISIBLE;
}

// 对外接口统一调用
if (!param_is_visible(addr)) {
    return PARAM_ERR_NOT_FOUND;
}
return g_param_table[addr].default_value;
```

---

## 10. C 结构体参考

```c
#define PARAM_COUNT              72
#define PARAM_ADDR_MIN           0
#define PARAM_ADDR_MAX           71
#define PARAM_BIN_HEADER_LEN     48
#define PARAM_PAYLOAD_HEADER_LEN 16
#define PARAM_RECORD_SIZE        12
#define PARAM_GCM_NONCE_LEN      12
#define PARAM_GCM_TAG_LEN        16
#define PARAM_NAME_MAX_BYTES     96

typedef enum {
    PARAM_TYPE_CONTROL    = 0,
    PARAM_TYPE_PROTECTION = 1,
} param_type_t;

typedef enum {
    PARAM_PERMISSION_HIDDEN  = 0,
    PARAM_PERMISSION_VISIBLE = 1,
} param_permission_t;

typedef struct {
    uint8_t  address;        /* 0 ~ 71                          */
    uint16_t default_value;  /* 0 ~ 65535                       */
    param_type_t       type;        /* 0 控制 / 1 保护          */
    param_permission_t permission;  /* 0 隐藏 / 1 可见          */
    const char        *name;        /* 指向 Name Table 的指针  */
    uint16_t           name_len;    /* UTF-8 字节长度          */
} param_item_t;

/* Header 解析后保存在 RAM 中，方便调试与回显 */
typedef struct {
    uint32_t product_id;
    uint32_t key_id;
    uint32_t flags;
    uint8_t  nonce[PARAM_GCM_NONCE_LEN];
    uint32_t payload_len;
    uint8_t  tag_len;
} param_bin_header_t;
```

---

## 11. 错误返回约定（建议）

ESP32 端解析函数建议返回以下枚举，方便上层统一处理：

```c
typedef enum {
    PARAM_BIN_OK              =  0,
    PARAM_BIN_ERR_IO          = -1, /* 文件读取失败            */
    PARAM_BIN_ERR_TOO_SMALL   = -2, /* 文件长度不足            */
    PARAM_BIN_ERR_MAGIC       = -3, /* magic / header 字段错   */
    PARAM_BIN_ERR_VERSION     = -4, /* 版本不支持              */
    PARAM_BIN_ERR_CRYPTO      = -5, /* 解密 / tag 校验失败     */
    PARAM_BIN_ERR_KEY_MISSING = -6, /* 找不到密钥              */
    PARAM_BIN_ERR_PAYLOAD     = -7, /* Payload 格式错误        */
    PARAM_BIN_ERR_ADDR        = -8, /* 地址越界 / 重复 / 缺失  */
    PARAM_BIN_ERR_NAME        = -9, /* 名称 UTF-8 错误 / 越界 */
} param_bin_result_t;
```

返回值含义与 PC 上位机的错误信息一致，便于跨端联调。

---

## 12. PC 端代码参考

PC 端实现参考：

```text
src-tauri/src/bin_format.rs     // 48-byte Header + AES-GCM Payload
src-tauri/src/crypto.rs         // PRODUCT_KEY + encrypt/decrypt_payload_with_aad
src-tauri/src/payload_codec.rs  // Payload Header + 72 Records + Name Table
src-tauri/src/validator.rs      // 参数合法性校验
src-tauri/src/tests.rs          // 14 个单元测试覆盖所有关键路径
```

完整的二进制协议、单测覆盖与端到端解析示例，都在上面的 Rust 文件里。

---

## 13. 版本

```text
Header  format_version = 1
Payload schema_version = 1
```

后续版本扩展只能 **新增字段** 并保留旧字段向后兼容，**禁止**修改任何
已有字段的偏移或长度。
