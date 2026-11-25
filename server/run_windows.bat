@echo off
setlocal EnableExtensions

REM ============================================================================
REM VortexBrowser Native Windows 10/11 Companion Server Launcher
REM ============================================================================

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

REM Refresh PATH in case ffmpeg or Python was installed via winget in a recent session
for /f "usebackq tokens=*" %%P in (`powershell -NoProfile -Command "[System.Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [System.Environment]::GetEnvironmentVariable('Path','User')" 2^>nul`) do (
    if not "%%P"=="" set "PATH=%%P;%PATH%"
)

set "VENV_PY=%SCRIPT_DIR%.venv\Scripts\python.exe"
if not exist "%VENV_PY%" (
    echo [!] Virtual environment .venv not found. Running install_windows.ps1 first...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%install_windows.ps1"
    if errorlevel 1 (
        echo [ERROR] Installation failed.
        pause
        exit /b 1
    )
)

if "%VORTEX_PORT%"==""          set "VORTEX_PORT=7788"
if "%VORTEX_WIDTH%"==""         set "VORTEX_WIDTH=1920"
if "%VORTEX_HEIGHT%"==""        set "VORTEX_HEIGHT=1080"
if "%VORTEX_FPS%"==""           set "VORTEX_FPS=30"
if "%VORTEX_VIDEO_BITRATE%"=="" set "VORTEX_VIDEO_BITRATE=10000"
if "%VORTEX_JPEG_QUALITY%"==""  set "VORTEX_JPEG_QUALITY=85"
if "%VORTEX_ENGINE%"==""        set "VORTEX_ENGINE=chromium"
if "%VORTEX_LOG_LEVEL%"==""     set "VORTEX_LOG_LEVEL=INFO"

echo ============================================================================
echo  VortexBrowser Native Windows Companion Server (x64)
echo  Resolution: %VORTEX_WIDTH%x%VORTEX_HEIGHT% @ %VORTEX_FPS% FPS ^| Format: YUV420P
echo  GPU H.264:  Auto (NVENC / AMF / QSV / Media Foundation / libx264)
echo  Audio:      Auto (Windows WASAPI Loopback / VB-Cable DirectShow)
echo  Ports:      %VORTEX_PORT% (Primary) ^& 9757 (LAN)
echo  Press Ctrl+C to stop cleanly.
echo ============================================================================

if "%~1"=="" (
    "%VENV_PY%" "%SCRIPT_DIR%server.py" ^
        --host 0.0.0.0 ^
        --port %VORTEX_PORT% ^
        --width %VORTEX_WIDTH% ^
        --height %VORTEX_HEIGHT% ^
        --fps %VORTEX_FPS% ^
        --format yuv420p ^
        --video-bitrate %VORTEX_VIDEO_BITRATE% ^
        --jpeg-quality %VORTEX_JPEG_QUALITY% ^
        --codec auto ^
        --engine %VORTEX_ENGINE% ^
        --log-level %VORTEX_LOG_LEVEL%
) else (
    "%VENV_PY%" "%SCRIPT_DIR%server.py" %*
)

set "EXIT_CODE=%ERRORLEVEL%"
if not "%EXIT_CODE%"=="0" (
    echo.
    echo [VortexBrowser] Server exited with code %EXIT_CODE%.
    pause
)
exit /b %EXIT_CODE%
