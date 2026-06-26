Param Bin Tool
==============

This is a portable Windows executable build of the Param Bin Tool.

Usage
-----
1. Double-click `ParamBinTool.exe` to run the application.
2. The app opens with the "参数构建" (Parameter Builder) tab. Edit the 72
   parameters (address 0~71), then click "生成加密 bin" to export an
   encrypted .bin file.
3. Switch to "参数解析" (Parameter Parser) tab to decrypt and inspect any
   bin file produced by this tool.

Notes
-----
1. This tool is used to build and parse encrypted ESP32 parameter bin files.
2. Bin file format:
     - 48-byte plaintext Header (magic "UEPB", version 1, AES-256-GCM)
     - AES-256-GCM encrypted Payload (header is used as AAD)
   Chinese parameter names live inside the encrypted Payload, never in the
   Header.
3. AES-256-GCM uses a 32-byte fixed ProductKey compiled into both the PC
   tool and the ESP32 firmware. The key is never written to the bin file.
4. 72 parameters, fixed address range 0~71, fixed schema version 1.
5. This is NOT an installer. It is a directly runnable exe. No MSI, no NSIS.
6. On Windows 10/11 no extra runtime is needed. On older Windows systems the
   WebView2 Runtime may be required (downloadable from Microsoft).

Rebuilding
----------
If you want to rebuild the exe from source, run:

    cd tools
    build_release_exe.bat

This will:
    1. Check Node.js, npm, cargo
    2. Run `npm install`
    3. Run `npm run build` (frontend bundle into `dist/`)
    4. Run `cargo build --release` in `src-tauri/`
    5. Copy `src-tauri/target/release/param_bin_tool.exe` to `release/ParamBinTool.exe`
    6. Refresh this README

Tampering Detection
-------------------
Any single-byte change to either the Header (AAD) or the encrypted Payload
will cause the AES-GCM tag verification to fail, and the parser will return
the error "Bin 文件损坏、被篡改或密钥错误 (AES-GCM tag 校验失败)".

File layout (final exe):
    tools/release/ParamBinTool.exe
    tools/release/README.txt