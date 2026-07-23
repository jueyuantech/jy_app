cmake_minimum_required(VERSION 3.10)

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_MODULE_PATH "${PROJECT_ROOT}/cmake")

include(TargetPlatform)
include(OsSdk)
include(ProductOverlay)
include(TargetConfig)

jy_app_detect_target_platform(JY_APP_TARGET_PLATFORM)
message(STATUS "Selected target platform: ${JY_APP_TARGET_PLATFORM}")

jy_app_prepare_os_sdk()

if(JY_APP_TARGET_PLATFORM STREQUAL "arm")
    include(PlatformArm)
endif()

if(JY_APP_TARGET_PLATFORM STREQUAL "mingw")
    jy_app_prepare_windows_mingw_platform()
endif()

if(JY_APP_TARGET_PLATFORM STREQUAL "msvc")
    jy_app_prepare_windows_msvc_platform()
endif()

project(jy_app C)

jy_app_validate_target_toolchain("${JY_APP_TARGET_PLATFORM}")

execute_process(
    COMMAND git describe --tags --long --always --abbrev=8
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    OUTPUT_VARIABLE JY_APP_VERSION_INFO
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(JY_APP_VERSION_INFO STREQUAL "")
    set(JY_APP_VERSION_INFO "unknown")
endif()
execute_process(
    COMMAND git status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    OUTPUT_VARIABLE JY_APP_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT JY_APP_GIT_STATUS STREQUAL "")
    string(APPEND JY_APP_VERSION_INFO "-dirty")
endif()
set(JY_APP_VERSION_STRING "${JY_APP_VERSION_INFO}_${JY_APP_PRODUCT}" CACHE STRING "jy_app version string" FORCE)
message(STATUS "jy_app version: ${JY_APP_VERSION_STRING}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

jy_app_apply_product_overlay()

include(CommonSources)

if(JY_APP_TARGET_PLATFORM STREQUAL "linux")
    include(PlatformLinux)
elseif(JY_APP_TARGET_PLATFORM STREQUAL "macos")
    include(PlatformMacos)
elseif(JY_APP_TARGET_PLATFORM STREQUAL "mingw")
    include(PlatformMingw)
elseif(JY_APP_TARGET_PLATFORM STREQUAL "msvc")
    include(PlatformMsvc)
endif()

if(JY_APP_TARGET_PLATFORM STREQUAL "msvc")
    if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(_lvgl_warning_suppression_flags "/wd4018 /wd4244 /wd4267")
    else()
        set(_lvgl_warning_suppression_flags "-Wno-sign-compare -Wno-conversion")
    endif()
    set_source_files_properties(${LVGL_SOURCES} PROPERTIES
        COMPILE_FLAGS "${_lvgl_warning_suppression_flags}")
    unset(_lvgl_warning_suppression_flags)
endif()

if(NOT JY_APP_TARGET_PLATFORM STREQUAL "arm")
    set_source_files_properties("${PROJECT_ROOT}/common/floatair_run.c" PROPERTIES
        COMPILE_DEFINITIONS "lv_timer_handler=simulator_lv_timer_handler")
endif()

list(PREPEND TARGET_SOURCES ${PLATFORM_SOURCES})

add_executable(${TARGET_NAME} ${TARGET_SOURCES})

set_target_properties(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

if(JY_APP_TARGET_PLATFORM STREQUAL "msvc")
    # Keep the actual target build in the same English Visual Studio locale used
    # during CMake compiler checks.
    set_property(TARGET ${TARGET_NAME} PROPERTY
        C_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;VSLANG=1033")
endif()

target_include_directories(${TARGET_NAME} PRIVATE
    ${PLATFORM_INCLUDE_DIRS}
    ${COMMON_INCLUDE_DIRS}
)

target_compile_definitions(${TARGET_NAME} PRIVATE ${PLATFORM_COMPILE_DEFS})
target_compile_definitions(${TARGET_NAME} PRIVATE
    JY_APP_VERSION_STRING=\"${JY_APP_VERSION_STRING}\"
)

if(JY_APP_TARGET_PLATFORM STREQUAL "arm")
    jy_app_configure_arm_target(${TARGET_NAME})
else()
    jy_app_configure_simulator_target(${TARGET_NAME})
endif()
