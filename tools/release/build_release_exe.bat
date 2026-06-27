@echo off
setlocal enabledelayedexpansion

chcp 65001 >nul

echo ========================================
echo Param Bin Tool - Release EXE Builder
echo ========================================

REM This script lives in <project>\release\, but it operates on the project
REM files one level up (package.json, src-tauri\, ...). Move into the project
REM root and remember the resolved absolute path so the later `cd` calls and
REM output paths stay correct regardless of how the script was invoked.
cd /d "%~dp0.."
set "PROJECT_DIR=%CD%"

echo.
echo [1/8] Checking environment...

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
echo [2/8] Installing npm dependencies...
call npm install
if errorlevel 1 (
    echo [ERROR] npm install failed.
    pause
    exit /b 1
)

echo.
echo [3/8] Building frontend...
call npm run build
if errorlevel 1 (
    echo [ERROR] Frontend build failed.
    pause
    exit /b 1
)

echo.
echo [4/8] Running Rust tests...
cd /d "%PROJECT_DIR%\src-tauri"
call cargo test
if errorlevel 1 (
    echo [ERROR] Rust tests failed.
    pause
    exit /b 1
)

echo.
echo [5/8] Building Rust release exe...
call cargo build --release
if errorlevel 1 (
    echo [ERROR] Rust release build failed.
    pause
    exit /b 1
)

cd /d "%PROJECT_DIR%"

echo.
echo [6/8] Preparing release directory...
if not exist release (
    mkdir release
)

set EXE_SRC=%PROJECT_DIR%\src-tauri\target\release\param_bin_tool.exe
set EXE_DST=%PROJECT_DIR%\release\ParamBinTool.exe

if not exist "%EXE_SRC%" (
    echo [ERROR] Expected exe not found: %EXE_SRC%
    echo Please check Cargo package name and actual target exe name.
    pause
    exit /b 1
)

REM If a previous build is still running, the destination file is locked by
REM the OS and `copy /Y` fails with "The process cannot access the file
REM because it is being used by another process." Kill any running instance
REM first, then retry the copy a few times in case the handle takes a moment
REM to release.
echo Closing any running ParamBinTool.exe instances...
taskkill /F /IM ParamBinTool.exe >nul 2>nul
REM Give Windows a brief moment to release the file lock. Use `ping` to
REM sleep because `timeout` is shadowed by Git Bash's POSIX `timeout`
REM when this script is invoked through bash, which would print a
REM "invalid time interval" error and abort the retry loop.
ping -n 2 127.0.0.1 >nul

set COPY_ATTEMPTS=0
:copy_retry
set /a COPY_ATTEMPTS+=1
copy /Y "%EXE_SRC%" "%EXE_DST%" >nul
if not errorlevel 1 goto copy_done
if %COPY_ATTEMPTS% GEQ 5 (
    echo [ERROR] Failed to copy exe after %COPY_ATTEMPTS% attempts.
    echo The destination file may still be locked. Close any running
    echo ParamBinTool.exe / Explorer window and retry.
    pause
    exit /b 1
)
echo Copy attempt %COPY_ATTEMPTS% failed, retrying in 1s...
ping -n 2 127.0.0.1 >nul
goto copy_retry
:copy_done

echo.
echo [7/8] Writing release README...
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
echo For ESP32 firmware integration, see docs/bin_protocol.md.
echo.
) > "%PROJECT_DIR%\release\README.txt"

echo.
echo [8/8] Done.
echo ========================================
echo Release exe generated:
echo %~dp0ParamBinTool.exe
echo ========================================
echo.

pause
exit /b 0
