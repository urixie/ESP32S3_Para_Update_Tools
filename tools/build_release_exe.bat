@echo off
setlocal enabledelayedexpansion

chcp 65001 >nul

echo ========================================
echo Param Bin Tool - Release EXE Builder
echo ========================================

cd /d "%~dp0"

echo.
echo [1/7] Checking environment...

where node >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Node.js not found. Please install Node.js 18+.
    pause
    exit /b 1
)

where npm >nul 2>nul
if errorlevel 1 (
    echo [ERROR] npm not found.
    pause
    exit /b 1
)

where cargo >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Rust cargo not found. Please install Rust.
    pause
    exit /b 1
)

echo [OK] Environment check passed.

echo.
echo [2/7] Installing npm dependencies...
call npm install
if errorlevel 1 (
    echo [ERROR] npm install failed.
    pause
    exit /b 1
)

echo.
echo [3/7] Building frontend...
call npm run build
if errorlevel 1 (
    echo [ERROR] Frontend build failed.
    pause
    exit /b 1
)

echo.
echo [4/7] Building Rust release exe...
cd /d "%~dp0src-tauri"
call cargo build --release
if errorlevel 1 (
    echo [ERROR] Rust release build failed.
    pause
    exit /b 1
)

cd /d "%~dp0"

echo.
echo [5/7] Preparing release directory...
if not exist release (
    mkdir release
)

set EXE_SRC=src-tauri\target\release\param_bin_tool.exe
set EXE_DST=release\ParamBinTool.exe

if not exist "%EXE_SRC%" (
    echo [ERROR] Expected exe not found: %EXE_SRC%
    echo Please check Cargo package name and actual target exe name.
    pause
    exit /b 1
)

copy /Y "%EXE_SRC%" "%EXE_DST%" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy exe.
    pause
    exit /b 1
)

echo.
echo [6/7] Writing release README...
(
echo Param Bin Tool
echo ==============
echo.
echo This is a portable Windows executable build.
echo.
echo Usage:
echo   Double-click ParamBinTool.exe to run.
echo.
echo Notes:
echo   1. This tool is used to build and parse encrypted ESP32 parameter bin files.
echo   2. The generated bin file uses AES-256-GCM encryption.
echo   3. Chinese parameter names are stored in encrypted payload and should not appear as plaintext in the bin file.
echo   4. This is not an installer. It is a directly runnable exe.
echo   5. Windows WebView2 Runtime may be required on older Windows systems.
echo   6. Fixed 72 parameters, addresses 0~71, fixed 48-byte Header + AES-GCM Payload.
echo.
) > release\README.txt

echo.
echo [7/7] Done.
echo ========================================
echo Release exe generated:
echo %~dp0release\ParamBinTool.exe
echo ========================================
echo.

pause
exit /b 0