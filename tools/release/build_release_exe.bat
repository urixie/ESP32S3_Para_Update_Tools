@echo off
setlocal enabledelayedexpansion

chcp 65001 >nul

echo ========================================
echo UniEdge Release EXE Builder
echo ========================================

REM Optional:
REM   build_release_exe.bat --force-npm-install
REM Use this when you explicitly want to reinstall npm dependencies.
if /I "%~1"=="--force-npm-install" (
    set "FORCE_NPM_INSTALL=1"
) else (
    set "FORCE_NPM_INSTALL=0"
)

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

for /f "delims=" %%v in ('node -v') do set "NODE_VERSION=%%v"
for /f "tokens=1 delims=." %%v in ("!NODE_VERSION!") do set "NODE_MAJOR=%%v"
set "NODE_MAJOR=!NODE_MAJOR:v=!"

if !NODE_MAJOR! LSS 18 (
    echo [ERROR] Node.js 18+ is required. Current version: !NODE_VERSION!
    pause
    exit /b 1
)

for /f "delims=" %%v in ('npm -v') do set "NPM_VERSION=%%v"
for /f "delims=" %%v in ('cargo --version') do set "CARGO_VERSION=%%v"

echo [OK] Node.js !NODE_VERSION!
echo [OK] npm !NPM_VERSION!
echo [OK] !CARGO_VERSION!
echo [OK] Environment check passed.

echo.
echo [2/8] Checking npm dependencies...

set "NEED_NPM_INSTALL=0"
set "NPM_INSTALL_REASON="

if "%FORCE_NPM_INSTALL%"=="1" (
    set "NEED_NPM_INSTALL=1"
    set "NPM_INSTALL_REASON=forced by --force-npm-install"
)

if "!NEED_NPM_INSTALL!"=="0" (
    if not exist "%PROJECT_DIR%\node_modules\" (
        set "NEED_NPM_INSTALL=1"
        set "NPM_INSTALL_REASON=node_modules not found"
    )
)

if "!NEED_NPM_INSTALL!"=="0" (
    node -e "const fs=require('fs');const refs=['package.json','package-lock.json'].filter(f=>fs.existsSync(f));const stamp='node_modules/.deps-ok';if(!fs.existsSync(stamp))process.exit(1);const t=fs.statSync(stamp).mtimeMs;process.exit(refs.every(f=>fs.statSync(f).mtimeMs<=t)?0:1)" >nul 2>nul
    if errorlevel 1 (
        set "NEED_NPM_INSTALL=1"
        set "NPM_INSTALL_REASON=package.json or package-lock.json changed, or dependency marker missing"
    )
)

if "!NEED_NPM_INSTALL!"=="1" (
    echo [INFO] npm dependencies need install: !NPM_INSTALL_REASON!

    if exist "%PROJECT_DIR%\package-lock.json" (
        echo [INFO] package-lock.json found, running npm ci...
        call npm ci
    ) else (
        echo [INFO] package-lock.json not found, running npm install...
        call npm install
    )

    if errorlevel 1 (
        echo [ERROR] npm dependency installation failed.
        pause
        exit /b 1
    )

    node -e "require('fs').writeFileSync('node_modules/.deps-ok', new Date().toISOString())" >nul 2>nul
    if errorlevel 1 (
        echo [WARN] Failed to write npm dependency marker. Next build may reinstall dependencies.
    ) else (
        echo [OK] npm dependency marker updated.
    )
) else (
    echo [OK] npm dependencies are up to date. Skipped npm install.
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

set "EXE_SRC=%PROJECT_DIR%\src-tauri\target\release\param_bin_tool.exe"

if not exist "%EXE_SRC%" (
    echo [ERROR] Expected exe not found: %EXE_SRC%
    echo Please check Cargo package name and actual target exe name.
    pause
    exit /b 1
)

echo.
echo [7/8] Finalizing portable release...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%\release\finalize_release.ps1" -ProjectDir "%PROJECT_DIR%" -ExeSrc "%EXE_SRC%"
if errorlevel 1 (
    echo [ERROR] Release finalization failed.
    pause
    exit /b 1
)

echo.
echo [8/8] Done.
echo ========================================
echo Release finalization complete.
echo ========================================
echo.

pause
exit /b 0
