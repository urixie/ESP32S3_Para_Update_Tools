Param Bin Tool
==============

This is a portable Windows executable build.

Usage:
  Double-click ParamBinTool.exe to run.

Notes:
  1. This tool is used to build and parse encrypted ESP32 parameter bin files.
  2. The generated bin file uses AES-256-GCM encryption.
  3. Chinese parameter names are stored in encrypted payload and should not appear as plaintext in the bin file.
  4. This is not an installer. It is a directly runnable exe.
  5. Windows WebView2 Runtime may be required on older Windows systems.
  6. Fixed 72 parameters, addresses 0~71, fixed 48-byte Header + AES-GCM Payload.

For ESP32 firmware integration, see docs/bin_protocol.md.

