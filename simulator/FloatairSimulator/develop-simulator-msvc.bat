@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "FALLBACK_DIR=C:\Program Files\Microsoft Visual Studio\18\Community"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%develop-simulator.ps1" %* --platform msvc --fallback-dir "%FALLBACK_DIR%"
exit /b %ERRORLEVEL%
