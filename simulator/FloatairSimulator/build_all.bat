@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "DEVELOP_SCRIPT=%SCRIPT_DIR%develop-simulator.ps1"
set "DRY_RUN=0"
set "BUILD_COUNT=0"
set "FAIL_COUNT=0"
set "NEEDS_SEPARATOR=0"
set "PRODUCT_NAME="
set "OS_SDK_ARCHIVE="

call :init_colors
call :init_pause_behavior

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--dry-run" (
    set "DRY_RUN=1"
    shift
    goto parse_args
)
if /I "%~1"=="--no-pause" (
    set "PAUSE_ON_EXIT=0"
    shift
    goto parse_args
)
if /I "%~1"=="--product" (
    if "%~2"=="" (
        echo [ERROR] --product requires a value.
        goto :finish_1
    )
    set "PRODUCT_NAME=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--help" (
    call :usage
    goto :finish_0
)
if /I "%~1"=="-h" (
    call :usage
    goto :finish_0
)
set "ARG=%~1"
if not defined OS_SDK_ARCHIVE if not "!ARG:~0,1!"=="-" (
    set "OS_SDK_ARCHIVE=%~1"
    shift
    goto parse_args
)
echo [ERROR] Unknown argument: %~1
call :usage
goto :finish_1

:args_done
if not exist "%DEVELOP_SCRIPT%" (
    echo [ERROR] Missing script: "%DEVELOP_SCRIPT%"
    goto :finish_1
)

if not defined PRODUCT_NAME (
    call :select_product
    if errorlevel 1 goto :finish_1
    call :select_os_sdk_archive
)
echo [INFO] Selected product: "%PRODUCT_NAME%"
if defined OS_SDK_ARCHIVE echo [INFO] Using OS SDK archive: "!OS_SDK_ARCHIVE!"
if not defined OS_SDK_ARCHIVE echo [INFO] Using newest OS SDK cache.

echo [INFO] Detecting supported Windows simulator builds...

call :check_mingw x86 gcc
if errorlevel 1 exit /b 1
call :check_mingw x86 llvm
if errorlevel 1 exit /b 1
call :check_msvc x86 cl
if errorlevel 1 exit /b 1
call :check_msvc x86 clang
if errorlevel 1 exit /b 1
call :check_msvc x86 clang-cl
if errorlevel 1 exit /b 1
call :check_mingw x64 gcc
if errorlevel 1 exit /b 1
call :check_mingw x64 llvm
if errorlevel 1 exit /b 1
call :check_msvc x64 cl
if errorlevel 1 exit /b 1
call :check_msvc x64 clang
if errorlevel 1 exit /b 1
call :check_msvc x64 clang-cl
if errorlevel 1 exit /b 1

if "%BUILD_COUNT%"=="0" (
    call :status_error "No supported simulator build was detected."
    goto :finish_1
)

call :print_separator_if_needed
if %FAIL_COUNT% GTR 0 (
    call :status_error "Some simulator builds failed. Failed count: %FAIL_COUNT% / %BUILD_COUNT%"
    for /l %%I in (1,1,%FAIL_COUNT%) do echo [FAILED] !FAILED_BUILD_%%I!
    goto :finish_1
)

if "%DRY_RUN%"=="1" (
    call :status_success "Dry run finished. Supported build count: %BUILD_COUNT%"
) else (
    call :status_success "All supported simulator builds finished. Build count: %BUILD_COUNT%"
)
goto :finish_0

:usage
echo Usage: build_all.bat [OS_SDK_ARCHIVE] [--dry-run] [--no-pause] [--product PRODUCT]
echo.
echo Detects and builds supported Windows simulator combinations using:
echo   develop-simulator.ps1
exit /b 0

:init_colors
set "COLOR_GREEN="
set "COLOR_RED="
set "COLOR_RESET="
if defined NO_COLOR exit /b 0
for /f "tokens=1 delims=#" %%E in ('"prompt #$E# & echo on & for %%B in (1) do rem"') do set "ESC=%%E"
set "COLOR_GREEN=!ESC![32m"
set "COLOR_RED=!ESC![31m"
set "COLOR_RESET=!ESC![0m"
exit /b 0

:status_yes
set "STATUS_TEXT=%COLOR_GREEN%yes%COLOR_RESET%"
exit /b 0

:status_no
set "STATUS_TEXT=%COLOR_RED%no%COLOR_RESET%"
exit /b 0

:status_success
echo %COLOR_GREEN%[SUCCESS]%COLOR_RESET% %~1
exit /b 0

:status_error
echo %COLOR_RED%[ERROR]%COLOR_RESET% %~1
exit /b 0

:init_pause_behavior
set "PAUSE_ON_EXIT=1"
if defined FLOATAIR_NO_PAUSE set "PAUSE_ON_EXIT=0"
exit /b 0

:finish_0
set "EXIT_CODE=0"
goto :finish

:finish_1
set "EXIT_CODE=1"
goto :finish

:finish
if "%PAUSE_ON_EXIT%"=="1" pause
exit /b %EXIT_CODE%

:run_build
set /a BUILD_COUNT+=1
set "BUILD_LABEL=%~1"
shift
set "RUN_ARGS="

:run_build_args_loop
if "%~1"=="" goto run_build_args_done
set "RUN_ARGS=!RUN_ARGS! "%~1""
shift
goto run_build_args_loop

:run_build_args_done
if defined PRODUCT_NAME set "RUN_ARGS=!RUN_ARGS! --product %PRODUCT_NAME%"
if "%DRY_RUN%"=="1" (
    if defined OS_SDK_ARCHIVE (
        echo [DRY-RUN] powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%DEVELOP_SCRIPT%" "!OS_SDK_ARCHIVE!" !RUN_ARGS! "--no-pause"
    ) else (
        echo [DRY-RUN] powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%DEVELOP_SCRIPT%" !RUN_ARGS! "--no-pause"
    )
    set "NEEDS_SEPARATOR=1"
    exit /b 0
)
echo [BUILD] %BUILD_LABEL%
if defined OS_SDK_ARCHIVE (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%DEVELOP_SCRIPT%" "!OS_SDK_ARCHIVE!" !RUN_ARGS! "--no-pause"
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%DEVELOP_SCRIPT%" !RUN_ARGS! "--no-pause"
)
set "BUILD_RESULT=%ERRORLEVEL%"
if not "%BUILD_RESULT%"=="0" (
    set /a FAIL_COUNT+=1
    set "FAILED_BUILD_!FAIL_COUNT!=%BUILD_LABEL%"
    call :status_error "Build failed: %BUILD_LABEL%"
)
set "NEEDS_SEPARATOR=1"
exit /b 0

:skip_build
echo [SKIP] %~1: %~2
set "NEEDS_SEPARATOR=1"
exit /b 0

:print_separator_if_needed
if "%NEEDS_SEPARATOR%"=="1" (
    echo.
    set "NEEDS_SEPARATOR=0"
)
exit /b 0

:check_mingw
call :print_separator_if_needed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%DEVELOP_SCRIPT%" --platform mingw --compiler %~2 --arch %~1 --check-platform-deps --no-pause >nul 2>nul
set "CHECK_RESULT=%ERRORLEVEL%"
if "%CHECK_RESULT%"=="0" (
    call :status_yes
    echo [CHECK] MinGW %~1 %~2: !STATUS_TEXT!
    call :run_build "Windows MinGW %~1 %~2" --platform mingw --compiler %~2 --arch %~1 --prefix "install\mingw-%~1-%~2" --no_run
) else (
    call :status_no
    echo [CHECK] MinGW %~1 %~2: !STATUS_TEXT!
    call :skip_build "Windows MinGW %~1 %~2" "platform dependencies are not available"
)
exit /b 0

:select_product
set "PRODUCT_COUNT=0"
echo.
echo Select product:
for /f "delims=" %%P in ('dir /b /ad "%SCRIPT_DIR%..\..\products" 2^>nul') do (
    set /a PRODUCT_COUNT+=1
    set "PRODUCT_!PRODUCT_COUNT!=%%P"
    echo   !PRODUCT_COUNT!. %%P
)

if "%PRODUCT_COUNT%"=="0" (
    echo [ERROR] No products found in products directory.
    exit /b 1
)

:select_product_prompt
set "PRODUCT_CHOICE="
set /p "PRODUCT_CHOICE=Enter product number: "
if not defined PRODUCT_CHOICE goto select_product_prompt
set /a PRODUCT_INDEX=PRODUCT_CHOICE 2>nul
if not "!PRODUCT_INDEX!"=="%PRODUCT_CHOICE%" (
    echo [ERROR] Invalid product selection.
    goto select_product_prompt
)
if !PRODUCT_INDEX! LSS 1 (
    echo [ERROR] Invalid product selection.
    goto select_product_prompt
)
if !PRODUCT_INDEX! GTR !PRODUCT_COUNT! (
    echo [ERROR] Invalid product selection.
    goto select_product_prompt
)

for %%I in (!PRODUCT_INDEX!) do set "PRODUCT_NAME=!PRODUCT_%%I!"
exit /b 0

:select_os_sdk_archive
echo.
set "OS_SDK_ARCHIVE="
set /p "OS_SDK_ARCHIVE=Enter OS SDK archive path (empty to use newest cache): "
if defined OS_SDK_ARCHIVE if "!OS_SDK_ARCHIVE:~0,1!"=="""" set "OS_SDK_ARCHIVE=!OS_SDK_ARCHIVE:~1!"
if defined OS_SDK_ARCHIVE if "!OS_SDK_ARCHIVE:~-1!"=="""" set "OS_SDK_ARCHIVE=!OS_SDK_ARCHIVE:~0,-1!"
exit /b 0

:check_msvc
call :print_separator_if_needed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%DEVELOP_SCRIPT%" --platform msvc --compiler %~2 --arch %~1 --check-platform-deps --no-pause >nul 2>nul
set "CHECK_RESULT=%ERRORLEVEL%"
if "%CHECK_RESULT%"=="0" (
    call :status_yes
    echo [CHECK] MSVC %~1 %~2: !STATUS_TEXT!
    call :run_build "Windows MSVC %~1 %~2" --platform msvc --compiler %~2 --arch %~1 --prefix "install\msvc-%~1-%~2" --no_run
) else (
    call :status_no
    echo [CHECK] MSVC %~1 %~2: !STATUS_TEXT!
    call :skip_build "Windows MSVC %~1 %~2" "platform dependencies are not available"
)
exit /b 0
