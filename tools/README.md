# 驱动器参数配置上位机

## 1. 项目概述

驱动器参数配置上位机 是一个基于 **Tauri + Rust + React + TypeScript** 的 ESP32 参数加密 bin 上位机。

工具用于编辑固定 72 个参数，并生成 ESP32 端可解析的加密 `.bin` 文件。加密文件中，参数中文名称、默认值、类型和权限全部位于 AES-256-GCM 加密 Payload 内，第三方工具无法直接解析出明文内容。

当前产品场景为：

```text
单个产品
单个客户
单个固定密钥
上位机与 ESP32 固件一一配套
```

因此本版本采用精简文件头，不再在 bin Header 中保留 Product ID / Key ID / 参数数量 / 地址范围等扩展字段。

---

## 2. 核心功能

- 固定 72 个参数，地址固定为 `0~71`
- 参数字段：
  - 名称：中文，最多 20 个字符
  - 地址：固定 0~71，不允许增删
  - 默认值：`u16`，范围 `0~65535`
  - 类型：控制 / 保护
  - 权限：可见 / 隐藏
- 支持工程文件保存与加载
- 支持导出 AES-256-GCM 加密 `.bin`
- 支持解析本工具生成的 `.bin`
- 支持校验参数数量、地址、名称和默认值
- 支持篡改检测：Header、密文或 Tag 任意位置被改动都会导致解析失败

---

## 3. 技术栈

### 前端

```text
React
TypeScript
Vite
Tauri API
```

### 后端

```text
Rust
Tauri 2.x
AES-256-GCM
serde / serde_json
```

---

## 4. 项目结构

```text
tools/
├── README.md
├── package.json
├── package-lock.json
├── index.html
├── vite.config.ts
├── tsconfig.json
├── tsconfig.node.json
├── src/
│   ├── App.tsx
│   ├── main.tsx
│   ├── components/
│   │   ├── AboutDialog.tsx
│   │   └── ParamTable.tsx
│   ├── pages/
│   │   ├── BuilderPage.tsx
│   │   └── ParserPage.tsx
│   ├── styles/
│   │   ├── app.css
│   │   └── theme.css
│   └── types/
│       └── parameter.ts
└── src-tauri/
    ├── Cargo.toml
    ├── tauri.conf.json
    └── src/
        ├── main.rs
        ├── commands.rs
        ├── error.rs
        ├── model.rs
        ├── project_file.rs
        ├── bin_format.rs
        ├── payload_codec.rs
        ├── crypto.rs
        └── tests.rs
```

---

## 5. 本地运行

### 安装依赖

```powershell
cd tools
npm install
```

### 启动前端开发模式

```powershell
npm run dev
```

### 启动 Tauri 桌面应用

```powershell
npm run tauri:dev
```

### Rust 测试

```powershell
cd tools\src-tauri
cargo test
```

---

## 6. 工程文件格式

工程文件是内部使用的明文 JSON，用于保存和回溯工程配置。

工程文件不是发布给 ESP32 的文件。

```ts
export interface ProjectFile {
  projectName: string;
  formatVersion: number;
  description: string;
  parameters: Parameter[];
}
```

说明：

```text
工程文件可以明文保存。
工程文件只用于内部工程师编辑和回溯。
真正给 ESP32 使用的是加密 bin 文件。
```

---

## 7. 加密 bin 文件格式

当前版本采用精简文件头：

```text
[17-byte Header][AES-GCM Ciphertext][16-byte GCM Tag]
```

### 7.1 Header 格式

Header 固定 17 字节，全部为明文。

| 偏移 | 字段 | 类型 | 大小 | 说明 |
|---:|---|---|---:|---|
| 0 | magic | `[u8; 4]` | 4 | 固定 `UEPB` |
| 4 | format_version | u8 | 1 | 当前固定为 `1` |
| 5 | nonce | `[u8; 12]` | 12 | AES-GCM 随机 Nonce |

总计：

```text
4 + 1 + 12 = 17 字节
```

Header 中不包含：

```text
Product ID
Key ID
参数数量
地址范围
Payload 长度
Tag 长度
中文名称
默认值
类型
权限
```

### 7.2 Payload 明文格式

Payload 在加密前使用固定二进制结构：

```text
+--------------------------+
| Payload Header           |
+--------------------------+
| Parameter Record[72]     |
+--------------------------+
| Name Table               |
+--------------------------+
```

#### Payload Header

固定 16 字节：

| 偏移 | 字段 | 类型 | 大小 | 说明 |
|---:|---|---|---:|---|
| 0 | payload_magic | `[u8; 4]` | 4 | 固定 `UPLD` |
| 4 | schema_version | u16 | 2 | 当前固定为 `1` |
| 6 | param_count | u8 | 1 | 固定 72 |
| 7 | record_size | u8 | 1 | 固定 12 |
| 8 | name_table_len | u16 | 2 | 中文名称表长度 |
| 10 | payload_flags | u16 | 2 | 预留，当前填 0 |
| 12 | payload_crc | u32 | 4 | 预留，当前填 0 |

#### Parameter Record

每个参数记录固定 12 字节：

| 偏移 | 字段 | 类型 | 大小 | 说明 |
|---:|---|---|---:|---|
| 0 | address | u8 | 1 | 0~71 |
| 1 | param_type | u8 | 1 | 0=控制，1=保护 |
| 2 | permission | u8 | 1 | 0=隐藏，1=可见 |
| 3 | reserved0 | u8 | 1 | 填 0 |
| 4 | default_value | u16 | 2 | 默认值 |
| 6 | name_offset | u16 | 2 | 名称表偏移 |
| 8 | name_len | u16 | 2 | 名称 UTF-8 字节长度 |
| 10 | reserved1 | u16 | 2 | 填 0 |

72 个参数记录大小：

```text
72 * 12 = 864 字节
```

#### Name Table

Name Table 用于保存 72 个参数名称。

```text
[参数0名称][参数1名称][参数2名称]...
```

名称使用 UTF-8 编码，不强制以 `\0` 结尾。每个参数通过 `name_offset + name_len` 定位自己的名称。

Name Table 位于 AES-GCM 加密 Payload 内，所以 bin 文件中不会出现中文明文。

---

## 8. 加密方案

### 8.1 算法

```text
AES-256-GCM
```

参数：

```text
Key:   32 bytes
Nonce: 12 bytes
Tag:   16 bytes
AAD:   17-byte Header
```

### 8.2 密钥方案

当前版本使用单个固定产品密钥：

```text
PC 上位机 Rust 后端内置 PRODUCT_KEY
ESP32 固件内置相同 PRODUCT_KEY
bin 文件中不保存密钥，也不保存 Key ID
```

密钥只允许放在：

```text
Rust 后端
ESP32 固件
```

不要放在：

```text
React / TypeScript 前端
工程 JSON 文件
bin 文件 Header
```

### 8.3 加密流程

```text
1. 校验 72 个参数
2. 编码 Payload 明文
3. 生成 12 字节随机 Nonce
4. 构建 17 字节 Header：Magic + Version + Nonce
5. 使用固定 PRODUCT_KEY 对 Payload 执行 AES-256-GCM 加密
6. Header 作为 AAD 参与认证
7. 写入文件：[Header][Ciphertext][Tag]
```

### 8.4 解密流程

```text
1. 读取文件
2. 检查文件长度至少为 17 + 16 字节
3. 解析 17 字节 Header
4. 检查 magic == UEPB
5. 检查 format_version == 1
6. 取出 Header 中的 Nonce
7. 使用固定 PRODUCT_KEY 解密 Ciphertext + Tag
8. Header 作为 AAD 参与认证
9. Tag 校验失败则拒绝解析
10. 解密成功后解析 Payload
11. 校验 Payload Header、72 个 Parameter Record 和 Name Table 边界
```

---

## 9. ESP32 端解析建议

ESP32 端建议使用同样的结构解析：

```text
param_bin_loader.c
param_bin_loader.h
param_table.c
param_table.h
```

ESP32 解析流程：

```text
1. 从 LittleFS / FATFS / SPIFFS / SD 卡读取 bin 文件
2. 检查文件大小 >= 33 字节
3. 检查前 4 字节 magic == UEPB
4. 检查第 5 字节 format_version == 1
5. 读取 12 字节 nonce
6. 将 17 字节 Header 作为 AES-GCM AAD
7. 使用固件内置 PRODUCT_KEY 解密后续 ciphertext + tag
8. 解密失败则拒绝加载
9. 解密成功后解析 Payload Header
10. 检查 payload_magic == UPLD
11. 检查 schema_version == 1
12. 检查 param_count == 72
13. 检查 record_size == 12
14. 解析 72 个 Parameter Record
15. 检查 address 是否为 0~71 且不重复
16. 检查 name_offset/name_len 不越界
17. 构建运行时参数表
18. 清理解密临时缓存
```

ESP32 运行时参数结构示例：

```c
typedef enum {
    PARAM_TYPE_CONTROL = 0,
    PARAM_TYPE_PROTECTION = 1,
} param_type_t;

typedef enum {
    PARAM_PERMISSION_HIDDEN = 0,
    PARAM_PERMISSION_VISIBLE = 1,
} param_permission_t;

typedef struct {
    uint8_t address;
    uint16_t default_value;
    param_type_t type;
    param_permission_t permission;
    const char *name;
} param_item_t;
```

客户界面权限原则：

```text
隐藏参数不能只在网页前端隐藏。
ESP32 对外接口本身就不能返回隐藏参数。
```

示例：

```c
bool param_is_visible(uint8_t address)
{
    return g_param_table[address].permission == PARAM_PERMISSION_VISIBLE;
}
```

对外查询时：

```c
if (!param_is_visible(address)) {
    return PARAM_ERR_NOT_FOUND;
}
```

---

## 10. Rust 模块职责

### `model.rs`

```text
定义 Parameter / ParamType / ParamPermission / ProjectFile / ParsedBinInfo
```

### `validator.rs`

```text
校验参数数量
校验地址范围
校验地址连续性
校验名称
校验默认值
返回详细错误列表
```

### `project_file.rs`

```text
保存 .ueproj.json
打开 .ueproj.json
JSON 序列化/反序列化
```

### `payload_codec.rs`

```text
参数 Vec<Parameter> -> Payload 明文 bytes
Payload 明文 bytes -> Vec<Parameter>
校验 Payload Header
校验 Name Table 边界
```

### `crypto.rs`

```text
保存固定 PRODUCT_KEY
生成 Nonce
AES-256-GCM 加密
AES-256-GCM 解密
```

### `bin_format.rs`

```text
构建 17 字节 Header
解析 17 字节 Header
Header 作为 AAD
组装 bin 文件
解析 bin 文件
```

### `commands.rs`

```text
暴露 Tauri 命令给前端调用
```

---

## 11. 测试用例

必须保留基础单元测试：

```text
1. Payload 编码/解码一致
2. bin 导出/解析一致
3. bin 中搜索不到中文明文
4. 修改 Header 任意字节后解析失败
5. 修改密文或 Tag 任意字节后解析失败
6. magic 错误时解析失败
7. format_version 错误时解析失败
8. 地址重复、缺失、越界时校验失败
9. 参数名称为空或超过 20 个字符时校验失败
10. 默认工程必须能通过校验
```

---

## 12. 第一版验收标准

```text
[ ] 上位机可以新建工程
[ ] 工程自动包含 72 个参数
[ ] 参数地址固定为 0~71
[ ] 可以编辑中文名称
[ ] 可以编辑默认值 0~65535
[ ] 可以选择控制/保护
[ ] 可以选择可见/隐藏
[ ] 可以保存 .ueproj.json
[ ] 可以打开 .ueproj.json
[ ] 可以生成加密 .bin
[ ] bin 文件采用 17 字节简化 Header
[ ] bin 文件中搜索不到中文明文
[ ] 可以解析生成的 .bin
[ ] 解析结果和原始工程一致
[ ] 修改 bin 任意字节后解析失败
[ ] UI 为深色工程技术风格
[ ] 代码有基础单元测试
```

---

## 13. 重要注意事项

```text
1. 不要把密钥放在前端 TypeScript 代码里。
2. 不要使用 Base64、XOR、压缩代替加密。
3. 不要让 bin 文件中出现中文明文。
4. 不要让地址范围变成 0~72。
5. 不要让参数数量变成 73。
6. 不要让 ESP32 客户接口返回隐藏参数。
7. 不要只在网页前端隐藏 Hidden 参数。
8. 不要直接把 JSON 给 ESP32 解析。
9. Header 中只允许保留 Magic、Version、Nonce。
10. 不要忽略文件篡改检测。
```
