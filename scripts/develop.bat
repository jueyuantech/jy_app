@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0.."

set "SCRIPT_NAME=%~nx0"
set "BUILD_DIR=build"
set "CLEAN_BUILD=0"
set "PRODUCT_NAME="
set "OS_SDK_ARCHIVE="

:parse_args
if "%~1"=="" goto after_args
if /I "%~1"=="--clean" (
    set "CLEAN_BUILD=1"
    shift
    goto parse_args
)
if /I "%~1"=="--build-dir" (
    if "%~2"=="" (
        echo [ERROR] --build-dir requires a value.
        exit /b 1
    )
    set "BUILD_DIR=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--product" (
    if "%~2"=="" (
        echo [ERROR] --product requires a value.
        exit /b 1
    )
    set "PRODUCT_NAME=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--help" (
    echo Usage: %SCRIPT_NAME% [OS_SDK_ARCHIVE] [--clean] [--build-dir DIR] [--product PRODUCT]
    exit /b 0
)
if /I "%~1"=="-h" (
    echo Usage: %SCRIPT_NAME% [OS_SDK_ARCHIVE] [--clean] [--build-dir DIR] [--product PRODUCT]
    exit /b 0
)
set "ARG=%~1"
if not defined OS_SDK_ARCHIVE if not "!ARG:~0,1!"=="-" (
    set "OS_SDK_ARCHIVE=%~1"
    shift
    goto parse_args
)

echo [ERROR] Unknown argument: %~1
echo Usage: %SCRIPT_NAME% [OS_SDK_ARCHIVE] [--clean] [--build-dir DIR] [--product PRODUCT]
exit /b 1

:after_args
if not defined PRODUCT_NAME (
    call :select_product
    if errorlevel 1 exit /b 1
    call :select_os_sdk_archive
)

set "PYTHON_EXE="

if defined PYTHON3_EXECUTABLE (
    set "PYTHON_EXE=%PYTHON3_EXECUTABLE%"
)

if not defined PYTHON_EXE (
    for /f "usebackq delims=" %%I in (`py -3 -c "import sys; print(sys.executable)" 2^>nul`) do set "PYTHON_EXE=%%I"
)

if not defined PYTHON_EXE (
    for /f "usebackq delims=" %%I in (`python -c "import sys; print(sys.executable)" 2^>nul`) do set "PYTHON_EXE=%%I"
)

if not defined PYTHON_EXE (
    echo [ERROR] Python 3 was not found. Install Python or set PYTHON3_EXECUTABLE.
    pause
    exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cmake was not found in PATH.
    pause
    exit /b 1
)

where ninja >nul 2>nul
if errorlevel 1 (
    echo [ERROR] ninja was not found in PATH.
    pause
    exit /b 1
)

echo [INFO] Using Python: "%PYTHON_EXE%"
echo [INFO] Selected product: "%PRODUCT_NAME%"
if defined OS_SDK_ARCHIVE echo [INFO] Using OS SDK archive: "!OS_SDK_ARCHIVE!"
if not defined OS_SDK_ARCHIVE echo [INFO] Using newest OS SDK cache.

"%PYTHON_EXE%" -c "import littlefs" >nul 2>nul
if errorlevel 1 (
    echo [INFO] Installing littlefs-python==0.15...
    "%PYTHON_EXE%" -m pip install littlefs-python==0.15
    if errorlevel 1 (
        pause
        exit /b 1
    )
)

if "%CLEAN_BUILD%"=="1" if exist "%BUILD_DIR%" (
    echo [INFO] Removing build directory "%BUILD_DIR%"...
    rmdir /s /q "%BUILD_DIR%"
)

echo [INFO] Configuring ARM build in "%BUILD_DIR%"...
if defined OS_SDK_ARCHIVE (
    cmake -S . -B "%BUILD_DIR%" -G Ninja -DPython3_EXECUTABLE="%PYTHON_EXE%" -DJY_APP_PRODUCT="%PRODUCT_NAME%" -DJY_APP_OS_SDK_ARCHIVE="!OS_SDK_ARCHIVE!"
) else (
    cmake -S . -B "%BUILD_DIR%" -G Ninja -DPython3_EXECUTABLE="%PYTHON_EXE%" -DJY_APP_PRODUCT="%PRODUCT_NAME%"
)
if errorlevel 1 (
    pause
    exit /b 1
)

echo [INFO] Building ARM target...
cmake --build "%BUILD_DIR%"
if errorlevel 1 (
    pause
    exit /b 1
)

echo [SUCCESS] Windows ARM build finished.
echo [SUCCESS] Outputs are in "%BUILD_DIR%".
pause
exit /b 0

:select_product
set "PRODUCT_COUNT=0"
echo.
echo Select product:
for /f "delims=" %%P in ('dir /b /ad "products" 2^>nul') do (
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
