# ESP32-S3 加密参数 bin Web 解析固件

该版本在原有 ESP32-S3 Web 文件管理固件基础上，新增了 tools 生成的加密参数 `.bin` 文件解析能力。

## 新增功能

- 保留 `/disk` FATFS 挂载、登录、上传、下载、删除功能。
- Web 左侧显示 `/disk` 根目录下的 `.bin` 文件列表。
- 点击某个 `.bin` 后，ESP32-S3 后端读取文件并执行 AES-256-GCM 解密认证。
- 解密成功后解析 Payload，显示 72 个参数：地址、名称、默认值、参数类型、权限。
- 密钥只保存在 ESP32 C 代码中，网页 HTML/JS 不包含密钥。

## 新增接口

| 接口 | 方法 | 说明 |
| --- | --- | --- |
| `/api/bin/parse?path=/xxx.bin` | GET | 解析指定加密参数 bin，返回 header 与 72 个参数 |
| `/api/param/readback` | POST | 使用 GPIO4/GPIO5 UART 从当前板卡回读 72 个参数，并返回当前 bin 的可见参数值 |
| `/api/param/download` | POST | 将当前 bin 的可见参数值合并到 72 参数数组后，通过 GPIO4/GPIO5 UART 写入板卡 |
| `/files` | GET | 文件列表中新增 `is_param_bin`、`kind`、`kind_label` 字段 |

## bin 解密兼容性

ESP32 端的解密规则与 tools 当前格式保持一致：

```text
[17-byte Header][AES-GCM ciphertext][16-byte GCM tag]
Header = "UEPB" + format_version(1) + nonce(12)
AAD = Header
KEY = tools/src-tauri/src/crypto.rs 中 PRODUCT_KEY
Payload = "UPLD" + schema + 72 条参数记录 + UTF-8 名称表
```

量产前请将 `components/app_param_bin/app_param_bin.c` 中的 `s_product_key` 替换为正式 32 字节随机密钥，并确保 tools 端同步替换。

## 参数板卡串口

参数回读和参数下载使用 `components/app_param_board/` 中的 UART 协议实现，默认连接为 `UART1 TX=GPIO4`、`UART1 RX=GPIO5`、`921600 8N1`。协议沿用板卡 82 字节帧格式，72 个 `uint16_t` 参数分两帧读写。

## 构建

本工程按 ESP-IDF 5.5.x 组织。首次使用建议：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

如果使用你原来的固定环境：

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\Espressif_5_5_4\.espressif\v5.5.4\esp-idf'
. "$env:IDF_PATH\export.ps1"
idf.py set-target esp32s3
idf.py build
```

## 默认登录

- 用户名：`admin`
- 密码：`admin`

正式产品建议改为 NVS 配置、一次性密码或设备授权码方案。

## Wi-Fi 配置

当前仍沿用原项目硬编码 STA 配置：

```c
#define APP_WIFI_SSID "UniEdge2022"
#define APP_WIFI_PASSWORD "uniedgehome"
```

路径：`components/app_wifi/app_wifi.c`
