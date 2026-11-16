cmake_minimum_required(VERSION 3.13)

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_MODULE_PATH "${PROJECT_ROOT}/cmake")

include(TargetPlatform)
include(OsSdk)
include(ProductOverlay)
include(TargetConfig)
include(ConfigOptions)

jy_app_detect_target_platform(JY_APP_TARGET_PLATFORM)
message(STATUS "Selected target platform: ${JY_APP_TARGET_PLATFORM}")

# Linux x86 的编译器 ABI 必须在 project() 探测前确定，避免 SDL 查找到 64 位系统库。
if(JY_APP_TARGET_PLATFORM STREQUAL "linux")
    if(DEFINED ARCH)
        string(TOLOWER "${ARCH}" _linux_target_arch)
    else()
        set(_linux_target_arch "x86")
    endif()
    if(_linux_target_arch MATCHES "^(x86|i386|i686|x86_32|32)$")
        string(APPEND CMAKE_C_FLAGS " -m32")
        string(APPEND CMAKE_CXX_FLAGS " -m32")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " -m32")
    endif()
    unset(_linux_target_arch)
endif()

jy_app_prepare_os_sdk()
jy_app_load_dot_config(CONFIG_COMPILE_DEFS)

if(JY_APP_TARGET_PLATFORM STREQUAL "arm")
    include(PlatformArm)
endif()

if(JY_APP_TARGET_PLATFORM STREQUAL "mingw")
    jy_app_prepare_windows_mingw_platform()
endif()

if(JY_APP_TARGET_PLATFORM STREQUAL "msvc")
    jy_app_prepare_windows_msvc_platform()
endif()

project(jy_app C CXX)

jy_app_validate_target_toolchain("${JY_APP_TARGET_PLATFORM}")

if(JY_APP_TARGET_PLATFORM STREQUAL "msvc" AND
   CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # 部分 MSVC 安装仅有中文语言包，VSLANG 无法强制英文；按实际输出字节探测依赖前缀。
    set(_jy_app_showincludes_dir "${CMAKE_BINARY_DIR}/CMakeFiles/JyShowIncludes")
    file(MAKE_DIRECTORY "${_jy_app_showincludes_dir}")
    file(WRITE "${_jy_app_showincludes_dir}/prefix.h" "\n")
    file(WRITE "${_jy_app_showincludes_dir}/prefix.c" "#include \"${_jy_app_showincludes_dir}/prefix.h\"\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env VSLANG=1033 --
                "${CMAKE_C_COMPILER}" /nologo /showIncludes /c prefix.c
        WORKING_DIRECTORY "${_jy_app_showincludes_dir}"
        OUTPUT_VARIABLE _jy_app_showincludes_output
        ERROR_VARIABLE _jy_app_showincludes_error
        RESULT_VARIABLE _jy_app_showincludes_result
        ENCODING NONE
    )
    if(NOT _jy_app_showincludes_result EQUAL 0 OR
       NOT _jy_app_showincludes_output MATCHES "(^|\n)([^:\r\n]+:[^:\r\n]*:[ \t]+)([A-Za-z]:[/\\\\]|/)")
        message(FATAL_ERROR "Cannot detect MSVC /showIncludes prefix: ${_jy_app_showincludes_error}")
    endif()
    # 中文 cl 输出可能是本地编码；Ninja 规则必须使用稳定的 ASCII 前缀。
    string(HEX "${CMAKE_MATCH_2}" _jy_app_showincludes_prefix_hex)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file: ")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # 前缀变化必须触发重编译，否则旧的零依赖记录会继续遗漏头文件更新。
    string(MD5 _jy_app_showincludes_tag "${_jy_app_showincludes_prefix_hex}")
    set(CMAKE_C_COMPILER_LAUNCHER
        "${CMAKE_COMMAND};-E;env;VSLANG=1033;JY_APP_MSVC_DEPENDENCY_TAG=${_jy_app_showincludes_tag};--;${Python3_EXECUTABLE};${PROJECT_ROOT}/scripts/msvc_showincludes.py;${_jy_app_showincludes_prefix_hex}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_C_COMPILER_LAUNCHER}")
    unset(_jy_app_showincludes_prefix_hex)
    unset(_jy_app_showincludes_tag)
    unset(_jy_app_showincludes_dir)
    unset(_jy_app_showincludes_output)
    unset(_jy_app_showincludes_error)
    unset(_jy_app_showincludes_result)
endif()

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


if(NOT JY_APP_TARGET_PLATFORM STREQUAL "arm")
    set_source_files_properties("${PROJECT_ROOT}/common/floatair_run.c" PROPERTIES
        COMPILE_DEFINITIONS "lv_timer_handler=simulator_lv_timer_handler")
endif()

set(TARGET_CONFIG_COMPILE_DEFS ${CONFIG_COMPILE_DEFS})
if(PLATFORM_CONFIG_OVERRIDES)
    jy_app_override_compile_defs(TARGET_CONFIG_COMPILE_DEFS
                                 TARGET_CONFIG_COMPILE_DEFS
                                 ${PLATFORM_CONFIG_OVERRIDES})
endif()

list(PREPEND TARGET_SOURCES ${PLATFORM_SOURCES})

add_executable(${TARGET_NAME} ${TARGET_SOURCES})
# 仅L8填充源文件接入自定义Helium；其他像素格式及Windows标量路径保持原实现。
set_property(SOURCE "${PROJECT_ROOT}/lvgl/src/draw/sw/blend/lv_draw_sw_blend_to_l8.c" APPEND PROPERTY
    COMPILE_DEFINITIONS
        LV_USE_DRAW_SW_ASM=LV_DRAW_SW_ASM_CUSTOM
        LV_DRAW_SW_ASM_CUSTOM_INCLUDE="common/lvgl_l8_mve.h"
)

# LVGL 独立编译为静态库，应用只链接库并继承同一份配置宏。
add_subdirectory("${LVGL_DIR}" "lvgl")
target_link_libraries(${TARGET_NAME} PRIVATE jy_lvgl)

set_target_properties(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

jy_app_configure_product_sync(${TARGET_NAME})

target_include_directories(${TARGET_NAME} PRIVATE
    ${PLATFORM_INCLUDE_DIRS}
    ${COMMON_INCLUDE_DIRS}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
    JY_APP_VERSION_STRING=\"${JY_APP_VERSION_STRING}\"
)

if(JY_APP_TARGET_PLATFORM STREQUAL "arm")
    jy_app_resolve_left_romfs(JY_APP_LEFT_ROMFS_DIR)
    jy_app_configure_arm_target(${TARGET_NAME})
else()
    jy_app_configure_simulator_target(${TARGET_NAME})
endif()
