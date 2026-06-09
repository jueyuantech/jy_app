@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "FALLBACK_DIR=D:\Tools\mingw32\bin"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%develop-simulator.ps1" %* --platform mingw --fallback-dir "%FALLBACK_DIR%"
exit /b %ERRORLEVEL%
