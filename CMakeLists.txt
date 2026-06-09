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

if(JY_APP_TARGET_PLATFORM STREQUAL "arm")
    jy_app_configure_arm_target(${TARGET_NAME})
else()
    jy_app_configure_simulator_target(${TARGET_NAME})
endif()
