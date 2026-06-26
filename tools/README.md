# Param Bin Tool

## 1. 项目概述

Param Bin Tool 是一款面向工程配置和参数发布流程的桌面工具，目标是将 72 个参数的构建、校验、封装与解析流程统一到一个可交付的软件系统中。当前版本以可运行原型为起点，重点建立参数编辑界面、工程数据模型以及后续加密 bin 文件生成链路的基础结构。

该工具面向 ESP32 参数配套场景，后续将用于将工程师配置的参数生成可分发的加密二进制文件，并支持工程侧对生成结果进行验证与回溯。

## 2. 产品目标

本项目的核心目标包括：

- 提供 72 个固定参数的工程化编辑能力
- 保证参数地址固定为 $0 \sim 71$
- 支持工程文件保存与参数配置回溯
- 为后续加密 `.bin` 文件生成与解析提供统一接口与数据结构
- 为 ESP32 端后续接入提供可扩展的参数协议基础

## 3. 功能范围

### 3.1 参数构建

- 提供参数构建主界面
- 支持编辑参数名称、默认值、类型与权限
- 保留固定参数数量与地址范围约束
- 为后续校验与导出流程提供基础交互层

### 3.2 参数解析

- 提供参数解析主界面
- 支持加载由本工具生成的加密参数文件
- 展示参数解析结果，便于工程侧检查内容与结构

### 3.3 工程管理

- 支持工程数据建模与文件化保存
- 为后续工程文件版本管理和回滚提供基础能力

## 4. 设计约束

当前版本遵循以下约束：

- 参数数量固定为 72
- 参数地址固定为 $0 \sim 71$
- 默认值类型为 16 位无符号整数，范围为 $0 \sim 65535$
- 参数类型分为控制类与保护类
- 权限分为可见与隐藏
- 发布文件必须以加密二进制格式承载，避免明文泄露

## 5. 技术方案

### 5.1 前端

- React + TypeScript + Vite
- 用于构建参数编辑界面、主工作区与状态展示
- 采用深色科技风主题，强调工程工具的可读性与稳定性

### 5.2 后端

- Tauri + Rust
- 用于承接桌面应用能力、文件访问、参数封装与后续加密逻辑
- 通过 Rust 层统一定义参数模型与二进制协议结构

### 5.3 数据与安全

- 工程文件以结构化格式保存，便于内部使用与调试
- 发布文件将以固定二进制格式封装，并在后续版本中引入加密与完整性校验
- 当前原型已建立基础加密与解密模块骨架，便于后续接入正式加密算法

## 6. 项目结构

```text
tools/
├── README.md
├── package.json
├── package-lock.json
├── index.html
├── vite.config.ts
├── tsconfig.json
├── tsconfig.node.json
├── vite-env.d.ts
├── src/
│   ├── App.tsx
│   ├── main.tsx
│   ├── components/
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

## 7. 开发环境

- Node.js 18+
- npm 9+
- Rust 1.78+
- Tauri 2.x 运行环境

## 8. 本地运行

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

## 9. 当前状态

当前版本已完成：

- 基础前端界面骨架
- 参数表格编辑能力
- 参数数据模型定义
- Rust 后端模块初始化
- 基础构建验证

## 10. 后续开发计划

下一阶段将重点推进以下内容：

1. 工程文件保存与加载
2. 参数合法性校验
3. 加密 `.bin` 文件生成
4. 加密文件解析与回显
5. 与 ESP32 端协议对齐

## 11. 交付说明

本项目当前属于工程原型阶段，重点是建立可演示、可扩展、可持续迭代的基础框架。后续将逐步完善为可用于实际参数发布流程的工程工具。

---

## 10. Header 格式

Header 固定长度建议为 48 字节。

| 偏移 | 字段 | 类型 | 大小 | 说明 |
|---:|---|---|---:|---|
| 0 | magic | `[u8;4]` | 4 | 固定 `UEPB` |
| 4 | header_len | u16 | 2 | 固定 48 |
| 6 | format_version | u16 | 2 | 第一版为 1 |
| 8 | crypto_algo | u8 | 1 | 1 = AES-256-GCM |
| 9 | param_count | u8 | 1 | 固定 72 |
| 10 | addr_min | u8 | 1 | 固定 0 |
| 11 | addr_max | u8 | 1 | 固定 71 |
| 12 | product_id | u32 | 4 | 产品ID |
| 16 | key_id | u32 | 4 | 密钥ID |
| 20 | flags | u32 | 4 | 预留，默认0 |
| 24 | nonce | `[u8;12]` | 12 | AES-GCM nonce |
| 36 | payload_len | u32 | 4 | 密文 payload 长度，不含 tag |
| 40 | tag_len | u8 | 1 | 固定 16 |
| 41 | reserved | `[u8;7]` | 7 | 预留，填0 |

总计：48 字节。

注意：Header 中不能放中文名称、默认值、类型、权限等敏感内容。

---

## 11. Payload 明文格式

Payload 在加密前使用固定二进制结构。

```text
+--------------------------+
| Payload Header           |
+--------------------------+
| Parameter Record[72]     |
+--------------------------+
| Name Table               |
+--------------------------+
```

## 11.1 Payload Header

建议固定 16 字节。

| 偏移 | 字段 | 类型 | 大小 | 说明 |
|---:|---|---|---:|---|
| 0 | payload_magic | `[u8;4]` | 4 | 固定 `UPLD` |
| 4 | schema_version | u16 | 2 | 第一版为 1 |
| 6 | param_count | u8 | 1 | 固定 72 |
| 7 | record_size | u8 | 1 | 固定 12 |
| 8 | name_table_len | u16 | 2 | 中文名称表长度 |
| 10 | payload_flags | u16 | 2 | 预留 |
| 12 | payload_crc | u32 | 4 | 可选，第一版可填0，因为 GCM 已认证 |

## 11.2 Parameter Record

每个参数记录固定 12 字节。

| 偏移 | 字段 | 类型 | 大小 | 说明 |
|---:|---|---|---:|---|
| 0 | address | u8 | 1 | 0~71 |
| 1 | param_type | u8 | 1 | 0=控制，1=保护 |
| 2 | permission | u8 | 1 | 0=隐藏，1=可见 |
| 3 | reserved0 | u8 | 1 | 填0 |
| 4 | default_value | u16 | 2 | 默认值 |
| 6 | name_offset | u16 | 2 | 名称表偏移 |
| 8 | name_len | u16 | 2 | 名称 UTF-8 字节长度 |
| 10 | reserved1 | u16 | 2 | 填0 |

总计：12 字节。

72 个参数记录大小：

```text
72 * 12 = 864 字节
```

## 12.3 Name Table

Name Table 用于保存 72 个参数名称。

所有名称使用 UTF-8 编码。建议不强制以 `\0` 结尾，因为每个参数记录已包含 `name_offset` 与 `name_len`。

示例：

```text
[母线过压保护阈值][母线欠压保护阈值][启动延时]...
```

每个参数通过 `name_offset + name_len` 进行定位。

## 13. 结论

Param Bin Tool 当前处于工程原型阶段，但已经形成了可持续迭代的基础骨架：前端交互、参数模型、Rust 后端模块与二进制封装思路均已落地。后续将以此为基础，逐步完善加密文件生成、解析校验与 ESP32 对接能力，最终形成可用于实际参数发布流程的工程工具。

找到名称。

Name Table 位于加密 Payload 中，所以 bin 文件中不会出现中文明文。

---

## 12. 加密方案

## 12.1 算法

使用：

```text
AES-256-GCM
```

参数：

```text
Key:   32 bytes
Nonce: 12 bytes
Tag:   16 bytes
AAD:   Header 48 bytes
```

### 加密流程

```text
1. 构建 Payload 明文
2. 生成 12 字节随机 nonce
3. 构建 Header，Header 中写入 nonce、payload_len 等信息
4. 使用 AES-256-GCM 加密 Payload
5. Header 作为 AAD
6. 输出 ciphertext + tag
7. 写入文件：Header + ciphertext + tag
```

### 解密流程

```text
1. 读取 Header
2. 校验 magic/header_len/format_version/param_count/addr_min/addr_max
3. 根据 product_id/key_id 选择密钥
4. 读取 ciphertext 和 tag
5. Header 作为 AAD
6. 使用 AES-256-GCM 解密
7. Tag 校验失败则返回“文件损坏或密钥错误”
8. 解密成功后解析 Payload
```

## 12.2 密钥方案

第一版使用产品级固定密钥：

```text
PC 上位机内置 ProductKey
ESP32 固件内置相同 ProductKey
bin 文件 Header 中只保存 key_id，不保存密钥
```

建议定义：

```text
product_id = 1
key_id = 1
key = 32 字节固定数组
```

密钥不要写在前端。只能放在 Rust 后端和 ESP32 固件中。

第一版可以硬编码，但要集中放在一个模块里，方便后续替换。

Rust 侧：

```rust
pub fn get_key_by_id(product_id: u32, key_id: u32) -> Option<[u8; 32]> {
    match (product_id, key_id) {
        (1, 1) => Some([0x11; 32]),
        _ => None,
    }
}
```

实际项目中不要使用 `[0x11; 32]`，应替换为真实随机密钥。

---

## 13. Rust 数据结构设计

## 13.1 参数模型

```rust
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct Parameter {
    pub name: String,
    pub address: u8,
    pub default_value: u16,
    pub param_type: ParamType,
    pub permission: Permission,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub enum ParamType {
    Control,
    Protection,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub enum Permission {
    Hidden,
    Visible,
}
```

## 13.2 工程模型

```rust
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ProjectFile {
    pub project_name: String,
    pub format_version: u16,
    pub product_id: u32,
    pub key_id: u32,
    pub description: String,
    pub parameters: Vec<Parameter>,
}
```

## 13.3 解析结果模型

```rust
#[derive(Debug, Clone, serde::Serialize)]
pub struct ParsedBinInfo {
    pub file_size: u64,
    pub format_version: u16,
    pub crypto_algo: String,
    pub product_id: u32,
    pub key_id: u32,
    pub param_count: u8,
    pub addr_min: u8,
    pub addr_max: u8,
    pub payload_len: u32,
    pub parameters: Vec<Parameter>,
}
```

---

## 14. Rust 模块职责

### 14.1 `model.rs`

职责：

```text
定义 Parameter / ParamType / Permission / ProjectFile / ParsedBinInfo
```

### 14.2 `validator.rs`

职责：

```text
校验参数数量
校验地址范围
校验地址连续性
校验名称
校验默认值
校验类型
校验权限
返回详细错误列表
```

### 14.3 `project_file.rs`

职责：

```text
保存 .ueproj
打开 .ueproj
JSON 序列化/反序列化
```

### 14.4 `payload_codec.rs`

职责：

```text
参数 Vec<Parameter> -> Payload 明文 bytes
Payload 明文 bytes -> Vec<Parameter>
校验 Payload Header
校验 Name Table 边界
```

### 14.5 `bin_format.rs`

职责：

```text
构建 Header
解析 Header
校验 Header
写入 bin 文件
读取 bin 文件
```

### 14.6 `crypto.rs`

职责：

```text
根据 product_id/key_id 获取密钥
AES-256-GCM 加密
AES-256-GCM 解密
nonce 生成
敏感数据清理
```

### 14.7 `commands.rs`

职责：

暴露 Tauri 命令给前端调用。

---

## 15. Tauri 命令接口设计

需要实现以下命令：

```rust
#[tauri::command]
pub fn new_project() -> Result<ProjectFile, String>;

#[tauri::command]
pub fn validate_project(project: ProjectFile) -> Result<Vec<ValidationMessage>, String>;

#[tauri::command]
pub fn save_project(project: ProjectFile, path: String) -> Result<(), String>;

#[tauri::command]
pub fn open_project(path: String) -> Result<ProjectFile, String>;

#[tauri::command]
pub fn export_encrypted_bin(project: ProjectFile, output_path: String) -> Result<(), String>;

#[tauri::command]
pub fn parse_encrypted_bin(input_path: String) -> Result<ParsedBinInfo, String>;
```

ValidationMessage：

```rust
#[derive(Debug, Clone, serde::Serialize)]
pub struct ValidationMessage {
    pub level: ValidationLevel,
    pub address: Option<u8>,
    pub field: Option<String>,
    pub message: String,
}

#[derive(Debug, Clone, serde::Serialize)]
pub enum ValidationLevel {
    Info,
    Warning,
    Error,
}
```

---

## 16. 校验规则

导出 bin 前必须通过校验。

强制规则：

```text
参数数量必须等于 72
地址必须为 0~71
地址不能重复
地址不能缺失
地址建议按 0~71 顺序排列
名称不能为空
名称必须是合法 UTF-8
名称 UTF-8 字节长度建议 <= 64
默认值必须为 0~65535
类型必须是 Control 或 Protection
权限必须是 Hidden 或 Visible
product_id 不能为 0
key_id 不能为 0
```

警告规则：

```text
名称过长但未超过硬限制时警告
全部参数都是可见时提示
全部参数都是隐藏时提示
保护类参数默认值为0时提示确认
```

---

## 17. 前端 TypeScript 类型

```ts
export type ParamType = 'Control' | 'Protection';
export type Permission = 'Hidden' | 'Visible';

export interface Parameter {
  name: string;
  address: number;
  default_value: number;
  param_type: ParamType;
  permission: Permission;
}

export interface ProjectFile {
  project_name: string;
  format_version: number;
  product_id: number;
  key_id: number;
  description: string;
  parameters: Parameter[];
}

export interface ValidationMessage {
  level: 'Info' | 'Warning' | 'Error';
  address?: number;
  field?: string;
  message: string;
}
```

---

## 18. ESP32 端兼容说明

上位机生成的 `.bin` 文件必须方便 ESP32 解析。

ESP32 端建议实现：

```text
param_bin_loader.c
param_bin_loader.h
param_table.c
param_table.h
```

ESP32 解析流程：

```text
1. 从 LittleFS / FATFS / SPIFFS / SD 卡读取 bin 文件
2. 检查文件至少大于 Header + Tag
3. 解析 48 字节 Header
4. 检查 magic == UEPB
5. 检查 format_version == 1
6. 检查 crypto_algo == 1
7. 检查 param_count == 72
8. 检查 addr_min == 0
9. 检查 addr_max == 71
10. 根据 product_id/key_id 获取密钥
11. Header 作为 AAD
12. AES-GCM 解密 ciphertext + tag
13. 解密失败则拒绝加载
14. 解析 Payload Header
15. 检查 payload_magic == UPLD
16. 检查 schema_version == 1
17. 检查 record_size == 12
18. 解析 72 个 Parameter Record
19. 检查 name_offset/name_len 不越界
20. 构建运行时参数表
21. 清理解密临时缓存
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
隐藏参数不能只在前端隐藏。
ESP32 对外接口本身就不能返回隐藏参数。
```

正确做法：

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

不要这样做：

```text
ESP32 API 返回全部参数，然后由网页前端隐藏 Hidden 参数。
```

因为客户可以抓包看到隐藏参数。

---

## 19. 错误处理策略

Rust 后端错误建议统一成：

```rust
#[derive(Debug, thiserror::Error)]
pub enum AppError {
    #[error("参数数量必须为72")]
    InvalidParameterCount,

    #[error("参数地址非法: {0}")]
    InvalidAddress(u8),

    #[error("参数名称不能为空: 地址 {0}")]
    EmptyName(u8),

    #[error("不是有效的参数bin文件")]
    InvalidMagic,

    #[error("文件版本不支持: {0}")]
    UnsupportedVersion(u16),

    #[error("密钥不存在: product_id={product_id}, key_id={key_id}")]
    KeyNotFound { product_id: u32, key_id: u32 },

    #[error("文件损坏、被篡改或密钥错误")]
    DecryptFailed,

    #[error("Payload格式错误")]
    InvalidPayload,
}
```

前端显示时分级：

```text
错误：阻止导出
警告：允许导出，但提醒
信息：普通提示
```

---

## 20. 测试用例

Codex 必须实现基础单元测试。

### 20.1 编码解码测试

1. 构造 72 个参数。
2. 编码成 Payload。
3. 再解码。
4. 比较前后数据一致。

### 20.2 加密解密测试

1. 构造 72 个参数。
2. 导出 bin bytes。
3. 解析 bin bytes。
4. 比较参数一致。

### 20.3 中文加密测试

1. 参数名称包含中文。
2. 导出 bin。
3. 直接搜索 UTF-8 中文字节。
4. bin 中不应包含中文明文字节。

### 20.4 篡改测试

1. 导出 bin。
2. 修改密文中任意一个字节。
3. 解析应失败。

### 20.5 Header 篡改测试

1. 导出 bin。
2. 修改 product_id 或 payload_len。
3. 解析应失败。

### 20.6 地址测试

1. 地址缺失时校验失败。
2. 地址重复时校验失败。
3. 地址大于 71 时校验失败。
4. 参数数量不是 72 时校验失败。

---

## 21. Codex 执行任务清单

请 Codex 按以下顺序实现，不要一次性乱改。

### 阶段 1：创建项目骨架

1. 创建 Tauri + React + TypeScript 项目。
2. 清理默认页面。
3. 建立前端目录结构。
4. 建立 Rust 后端模块结构。
5. 应用深色工程工具风格 CSS 变量。

### 阶段 2：实现 Rust 数据模型和校验

1. 实现 `model.rs`。
2. 实现 `validator.rs`。
3. 添加 72 参数默认模板生成函数。
4. 添加单元测试。

### 阶段 3：实现工程文件

1. 实现 `.ueproj` 保存。
2. 实现 `.ueproj` 打开。
3. 前端接入保存/打开按钮。

### 阶段 4：实现 Payload 编解码

1. 实现 Payload Header 编码。
2. 实现 Parameter Record 编码。
3. 实现 Name Table 编码。
4. 实现 Payload 解码。
5. 添加中文名称测试。

### 阶段 5：实现 AES-GCM 加解密

1. 实现密钥选择。
2. 实现 nonce 生成。
3. 实现加密。
4. 实现解密。
5. Header 作为 AAD。
6. 添加篡改测试。

### 阶段 6：实现 bin 文件导出/解析

1. 实现 Header 编码/解析。
2. 实现 `export_encrypted_bin`。
3. 实现 `parse_encrypted_bin`。
4. 前端接入导出和解析页面。

### 阶段 7：完善界面

1. 实现左侧 Sidebar。
2. 实现顶部 TopBar。
3. 实现参数构建表格。
4. 实现解析结果表格。
5. 实现校验结果面板。
6. 实现成功/失败 Toast 或状态卡片。
7. 优化表格颜色标签。

### 阶段 8：最终验收

1. 能新建 72 参数工程。
2. 能保存工程。
3. 能重新打开工程。
4. 能生成加密 bin。
5. bin 中搜索不到中文明文。
6. 能解析自己生成的 bin。
7. 修改 bin 任意字节后解析失败。
8. 解析结果与原参数一致。
9. UI 风格统一，接近工程技术站/工具站风格。

---

## 22. 给 Codex 的总提示词

可以直接把下面这段作为 Codex 的初始任务提示词：

```text
请根据当前 Markdown 设计文档，构建一个基于 Tauri + Rust + React + TypeScript 的加密参数 bin 上位机。

核心要求：
1. 固定 72 个参数，地址 0~71。
2. 每个参数包含：中文名称、地址、默认值 u16、类型 Control/Protection、权限 Visible/Hidden。
3. 上位机包含参数构建界面和参数解析界面。
4. 支持内部工程文件 .ueproj，工程文件可以明文 JSON。
5. 支持导出加密 .bin 文件，bin 文件由 48 字节明文 Header + AES-256-GCM 加密 Payload + 16 字节 Tag 组成。
6. Header 不允许包含中文名称、默认值、类型、权限等敏感内容。
7. Payload 包含 72 个 Parameter Record 和 UTF-8 中文 Name Table，整体加密。
8. bin 文件中不能出现任何中文明文。
9. Header 作为 AES-GCM AAD，修改 Header 或密文任意字节都必须导致解析失败。
10. 上位机内部解析界面可以显示全部参数，包括 Hidden 参数。
11. 后续 ESP32 端会解析该 bin，因此二进制协议必须固定、小端、简单、适合 C 语言解析。
12. UI 风格参考 https://urixie.github.io/，采用深色工程技术站风格，左侧导航 + 右侧工作区，使用卡片、矩阵、章节编号、青绿色科技感点缀。

请按阶段实现：先完成 Rust 数据模型、校验、Payload 编解码、AES-GCM 加解密和测试，再接入 Tauri 命令，最后实现前端界面。每个阶段完成后运行测试和构建，确保没有 TypeScript/Rust 编译错误。
```

---

## 23. 重要注意事项

1. 不要把密钥放在前端 TypeScript 代码里。
2. 不要使用 Base64、XOR、压缩代替加密。
3. 不要让 bin 文件中出现中文明文。
4. 不要让地址范围变成 0~72。
5. 不要让参数数量变成 73。
6. 不要让 ESP32 客户接口返回隐藏参数。
7. 不要只在网页前端隐藏 Hidden 参数。
8. 不要直接把 JSON 给 ESP32 解析。
9. 不要在 Header 中放任何敏感字段。
10. 不要忽略文件篡改检测。

---

## 24. 第一版验收标准

第一版完成后，必须满足：

```text
[ ] 上位机可以新建工程
[ ] 工程自动包含 72 个参数
[ ] 参数地址固定为 0~71
[ ] 可以编辑中文名称
[ ] 可以编辑默认值 0~65535
[ ] 可以选择控制/保护
[ ] 可以选择可见/隐藏
[ ] 可以保存 .ueproj
[ ] 可以打开 .ueproj
[ ] 可以校验参数
[ ] 可以生成加密 .bin
[ ] bin 文件中搜索不到中文明文
[ ] 可以解析生成的 .bin
[ ] 解析结果和原始工程一致
[ ] 修改 bin 任意字节后解析失败
[ ] UI 为深色工程技术风格
[ ] 代码有基础单元测试
```

