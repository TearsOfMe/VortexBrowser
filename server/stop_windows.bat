@echo off
setlocal EnableExtensions

REM ============================================================================
REM VortexRemoteBrowser Native Windows Background Server Stopper
REM Cleanly shuts down the invisible / tray server (saving cookies & closing Chromium)
REM ============================================================================

set "SCRIPT_DIR=%~dp0"
set "VENV_PY=%SCRIPT_DIR%.venv\Scripts\python.exe"
set "TRAY_PY=%SCRIPT_DIR%run_windows_tray.pyw"

if exist "%VENV_PY%" (
    "%VENV_PY%" "%TRAY_PY%" --stop
) else (
    python "%TRAY_PY%" --stop
)
exit /b %ERRORLEVEL%
