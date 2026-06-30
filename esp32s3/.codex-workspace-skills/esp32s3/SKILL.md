---
name: esp32s3-web-file-system
description: Project-specific workflow for the ESP32-S3 web file-system firmware in this workspace. Use when editing FAT storage mounting, Wi-Fi startup, HTTP login/file APIs, web file listing/upload/download/delete behavior, partition layout, ESP-IDF build validation, or Windows Chinese source search and encoding-safe edits in this project.
---

# ESP32-S3 Web File-System Firmware

## Scope

Use this skill for the project at:

`E:\ESP32S3_Para_Update_Tools\esp32s3`

This firmware is intentionally trimmed down to a Wi-Fi HTTP file manager backed by an internal wear-levelled FAT filesystem mounted at `/disk`.

Do not reintroduce LCD, ST7789, LVGL, Gui Guider screens, local key tasks, target-chip programming tasks, TinyUSB MSC, or ESP32 web OTA unless the user explicitly asks for those features.

## Source Encoding And Tools

For source reading and editing on Windows, prefer `rg` for search.

Avoid PowerShell text rewriting for source files that contain Chinese UI strings because the console code page can display mojibake. Use `apply_patch` for manual edits. If scripting is unavoidable, read and write UTF-8 explicitly.

If terminal output shows mojibake, verify against UTF-8 source bytes or build behavior instead of trusting console rendering.

## Windows Chinese Search Contract

On Windows, when searching Chinese source code, Chinese comments, Chinese UI strings, Chinese HTML text, or Chinese log strings, use `rg` first. Do not prefer PowerShell text search for Chinese content.

Use fixed-string search for ordinary keywords:

```powershell
rg -n -F "关键词" .
```

Use Unicode Han matching when searching for Chinese characters broadly:

```powershell
rg -n "\p{Han}+" .
```

Use file-type globs when the target surface is known:

```powershell
rg -n -F "关键词" -g "*.c" -g "*.h" .
rg -n -F "关键词" -g "*.html" -g "*.css" -g "*.js" .
```

Exclude build outputs and dependency directories by default:

```powershell
rg -n -F "关键词" . --glob "!build" --glob "!managed_components" --glob "!.git" --glob "!node_modules" --glob "!dist" --glob "!target"
```

If UTF-8 search does not find text but the user clearly says Chinese text exists in the code, try GBK:

```powershell
rg -n --encoding gbk -F "关键词" .
```

If a file may be UTF-16, try UTF-16LE:

```powershell
rg -n --encoding utf-16le -F "关键词" .
```

If terminal output looks garbled, do not assume the source file is garbled. Verify with `rg`, editor-visible content, build behavior, or Python with explicit encoding reads.

## Avoid These Windows Search Patterns

Unless the user explicitly asks for them, do not use these commands for Chinese source, comment, UI, HTML, or log string search:

```powershell
Select-String
findstr
Get-Content | Where-Object
Get-ChildItem -Recurse | Select-String
type file | findstr
```

These patterns can fail to match Chinese text or print mojibake in Windows console, PowerShell pipeline, and mixed GBK/UTF-8 source environments. A failed PowerShell search is not enough evidence that the Chinese code or string does not exist.

## Search Fallbacks

If `rg` is not available, check the tool first:

```powershell
where rg
rg --version
```

If `rg` is not installed, do not directly fall back to PowerShell Chinese text search. Prefer Python search with explicit encodings:

```powershell
python - <<'PY'
from pathlib import Path

keyword = "关键词"
roots = [Path(".")]
suffixes = {".c", ".h", ".html", ".css", ".js", ".ts", ".tsx", ".md", ".csv"}

skip_dirs = {".git", "build", "managed_components", "node_modules", "dist", "target"}

for root in roots:
    for path in root.rglob("*"):
        if any(part in skip_dirs for part in path.parts):
            continue
        if not path.is_file() or path.suffix.lower() not in suffixes:
            continue

        text = None
        for enc in ("utf-8", "gbk", "utf-16le"):
            try:
                text = path.read_text(encoding=enc)
                break
            except UnicodeDecodeError:
                continue
            except OSError:
                break

        if text is None:
            continue

        for i, line in enumerate(text.splitlines(), 1):
            if keyword in line:
                print(f"{path}:{i}:{line}")
PY
```

If the active PowerShell does not support heredoc-style input, create a temporary `.py` file and run it. The Python script must explicitly specify encodings. Do not use PowerShell pipelines to read Chinese source files.

## File Name Search

For filename discovery, prefer `fd`:

```powershell
fd app.html
fd SKILL.md
fd -e c
fd -e h
fd -e html
```

If `fd` is unavailable, use `rg --files`:

```powershell
rg --files
rg --files | rg -F "app.html"
```

Do not prefer:

```powershell
Get-ChildItem -Recurse
```

It is slower in large repositories, and Codex often mistakes PowerShell output encoding issues for source encoding problems.

## Mandatory Search Workflow Before Editing

Before editing any file that contains Chinese UI text, Chinese logs, Chinese comments, or Chinese web strings:

1. Confirm the target path with `rg --files` or `fd`.
2. Locate context with `rg -n -F "关键词" 文件路径`.
3. If there is no match but the user supplied a clear keyword, try:
   - `rg -n --encoding gbk -F "关键词" .`
   - `rg -n --encoding utf-16le -F "关键词" .`
   - Python explicit-encoding search.
4. Only say the code was not found after multiple search methods fail.
5. Never conclude the matching code does not exist from one failed PowerShell search.

## Safe Editing For Chinese Source Files

When editing files that contain Chinese text, prefer `apply_patch`.

Do not rewrite Chinese source files with PowerShell `Set-Content`, `Out-File`, or redirection `>`.

If a script must edit a Chinese-containing file:

- use `encoding="utf-8"` explicitly;
- preserve the existing newline style;
- after editing, run `rg -n "\p{Han}+" 文件路径` to confirm Chinese text is still readable.

Be especially careful with Chinese strings in `.html`, `.c`, `.h`, and `.css` files.

## Project Layout

- Runtime app entry: `main/main.c`
- Storage mount owner: `components/BSP/STORAGE/storage_flash.c`
- BSP startup: `components/BSP/bsp.c`
- Shared storage mutex: `components/app_storage_lock/`
- Wi-Fi STA startup and got-IP callback: `components/app_wifi/`
- HTTP login and file APIs: `components/app_web_file_server/`
- Partition table: `partitions.csv`

Legacy task, LCD, SPI, programmer, and desktop tool files may remain in the tree as references or host tools, but they are not part of the firmware runtime path.

## Runtime Contract

Startup flow:

1. `app_storage_lock_init()`
2. `bsp_init()`
3. mount FAT storage at `g_storage.disk_path`
4. start Wi-Fi STA
5. start the HTTP file server from the got-IP callback

The firmware should keep runtime dependencies narrow: `main` depends on BSP, storage lock, Wi-Fi, and the web file server only.

## File System And Web Server

The filesystem mount path is `/disk`, exposed through `g_storage.disk_path`.

Use `app_storage_lock()` / `app_storage_unlock()` for every web operation that reads, writes, deletes, or streams files from `/disk`.

Current web routes:

- `GET /`
- `GET /app`
- `POST /api/login`
- `POST /api/logout`
- `GET /api/auth/status`
- `GET /files`
- `GET /download?path=...`
- `POST /upload?filename=...`
- `POST /delete`
- `GET /favicon.ico`

Protected routes must call `web_auth_is_logged_in(req)` before touching files. `/`, `/api/login`, `/api/logout`, and `/api/auth/status` have their own auth behavior.

Keep path safety checks:

- URL-decode paths once.
- Strip leading `/` before appending to `/disk`.
- Reject `..`, backslash, colon, control characters, and invalid upload names.
- Skip `.` and `..` during directory listing.
- Keep JSON responses as `application/json; charset=utf-8`.

## Partition Model

Use a simple single-app layout with a large FAT data partition:

- `nvs`
- `phy_init`
- `factory`
- `storage` FAT partition mounted at `/disk`

Do not add `otadata` or OTA app slots unless ESP32 web OTA is restored intentionally.

## Build Validation

When validation is requested, run ESP-IDF build from the project root.

Prefer the local ESP-IDF environment if already configured. On this machine, a working fallback is usually:

```powershell
idf.py build
```

If component metadata or CMake cache is stale after dependency removal, run:

```powershell
idf.py reconfigure build
```

Useful interpretation:

- Missing LVGL/ST7789 headers after this trim means a runtime CMake path still points at old UI/task code.
- `ESP_ERR_HTTPD_HANDLERS_FULL` means `config.max_uri_handlers` is too small for registered routes.
- `format-truncation` warnings can be fatal; increase buffers or use bounded helpers.
- `dependencies.lock` may be regenerated by ESP-IDF. It should not contain `lvgl/lvgl`, `espressif/esp_lvgl_port`, or `espressif/button` for this firmware.
