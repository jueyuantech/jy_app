set(TOOLCHAIN_PREFIX "arm-none-eabi-" CACHE STRING "Cross compiler prefix for ARM builds")

set(_arm_toolchain_bin_hints)
if(DEFINED CMAKE_C_COMPILER AND NOT CMAKE_C_COMPILER STREQUAL "")
    get_filename_component(_preselected_arm_c_compiler_name "${CMAKE_C_COMPILER}" NAME)
    if(_preselected_arm_c_compiler_name MATCHES "^arm-none-eabi-gcc(\\.exe)?$")
        set(ARM_C_COMPILER "${CMAKE_C_COMPILER}")
        get_filename_component(_preselected_arm_toolchain_bin_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        list(APPEND _arm_toolchain_bin_hints "${_preselected_arm_toolchain_bin_dir}")
    endif()
endif()

if(NOT ARM_C_COMPILER)
    find_program(ARM_C_COMPILER ${TOOLCHAIN_PREFIX}gcc HINTS ${_arm_toolchain_bin_hints})
endif()
find_program(ARM_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc HINTS ${_arm_toolchain_bin_hints})
find_program(ARM_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++ HINTS ${_arm_toolchain_bin_hints})
find_program(ARM_AR ${TOOLCHAIN_PREFIX}ar HINTS ${_arm_toolchain_bin_hints})
find_program(ARM_LINKER ${TOOLCHAIN_PREFIX}ld HINTS ${_arm_toolchain_bin_hints})

if(NOT ARM_C_COMPILER)
    message(FATAL_ERROR "Could not find ${TOOLCHAIN_PREFIX}gcc. Please make sure the ARM GCC toolchain is installed and in PATH.")
endif()

set(CMAKE_SYSTEM_NAME Generic CACHE STRING "Cross-compilation target system" FORCE)
set(CMAKE_SYSTEM_PROCESSOR arm CACHE STRING "Cross-compilation target processor" FORCE)
set(CMAKE_C_COMPILER "${ARM_C_COMPILER}" CACHE FILEPATH "ARM C compiler" FORCE)
if(ARM_ASM_COMPILER)
    set(CMAKE_ASM_COMPILER "${ARM_ASM_COMPILER}" CACHE FILEPATH "ARM ASM compiler" FORCE)
endif()
if(ARM_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "${ARM_CXX_COMPILER}" CACHE FILEPATH "ARM CXX compiler" FORCE)
endif()
if(ARM_AR)
    set(CMAKE_AR "${ARM_AR}" CACHE FILEPATH "ARM archiver" FORCE)
endif()
if(ARM_LINKER)
    set(CMAKE_LINKER "${ARM_LINKER}" CACHE FILEPATH "ARM linker" FORCE)
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TARGET_NAME "uimain")
set(PLATFORM_DIR "${PROJECT_ROOT}/bes28")

function(resolve_nuttx_pointer_dir OUT_VAR POINTER_FILE BASE_DIR NUTTX_ROOT)
    if(IS_DIRECTORY "${POINTER_FILE}")
        set(${OUT_VAR} "${POINTER_FILE}" PARENT_SCOPE)
        return()
    endif()

    if(NOT EXISTS "${POINTER_FILE}")
        message(FATAL_ERROR "Required NuttX include entry '${POINTER_FILE}' does not exist.")
    endif()

    file(READ "${POINTER_FILE}" POINTER_CONTENT)
    string(STRIP "${POINTER_CONTENT}" POINTER_CONTENT)

    if(POINTER_CONTENT STREQUAL "")
        message(FATAL_ERROR "NuttX include entry '${POINTER_FILE}' is empty.")
    endif()

    unset(RESOLVED_DIR)

    if(IS_ABSOLUTE "${POINTER_CONTENT}" AND IS_DIRECTORY "${POINTER_CONTENT}")
        set(RESOLVED_DIR "${POINTER_CONTENT}")
    elseif(IS_ABSOLUTE "${POINTER_CONTENT}")
        string(REGEX REPLACE "^.*[/\\\\]nuttx[/\\\\]" "" POINTER_SUFFIX "${POINTER_CONTENT}")
        if(NOT POINTER_SUFFIX STREQUAL "${POINTER_CONTENT}")
            get_filename_component(RESOLVED_DIR "${NUTTX_ROOT}/${POINTER_SUFFIX}" ABSOLUTE)
        endif()
    else()
        get_filename_component(RESOLVED_DIR "${BASE_DIR}/${POINTER_CONTENT}" ABSOLUTE)
    endif()

    if(NOT RESOLVED_DIR OR NOT IS_DIRECTORY "${RESOLVED_DIR}")
        message(FATAL_ERROR
            "Failed to resolve NuttX include entry '${POINTER_FILE}' with content "
            "'${POINTER_CONTENT}'.")
    endif()

    set(${OUT_VAR} "${RESOLVED_DIR}" PARENT_SCOPE)
endfunction()

set(WINDOWS_NUTTX_COMPAT_INCLUDE_DIR "")
if(CMAKE_HOST_WIN32)
    set(NUTTX_COMPAT_ROOT "${CMAKE_BINARY_DIR}/nuttx_compat")
    file(REMOVE_RECURSE "${NUTTX_COMPAT_ROOT}")
    file(MAKE_DIRECTORY "${NUTTX_COMPAT_ROOT}")

    resolve_nuttx_pointer_dir(NUTTX_ARCH_SOURCE
        "${NUTTX_INCLUDE_DIR}/include/arch"
        "${NUTTX_INCLUDE_DIR}/include"
        "${NUTTX_INCLUDE_DIR}")
    file(COPY "${NUTTX_ARCH_SOURCE}" DESTINATION "${NUTTX_COMPAT_ROOT}")
    get_filename_component(NUTTX_ARCH_SOURCE_NAME "${NUTTX_ARCH_SOURCE}" NAME)
    if(NOT NUTTX_ARCH_SOURCE_NAME STREQUAL "arch")
        file(RENAME
            "${NUTTX_COMPAT_ROOT}/${NUTTX_ARCH_SOURCE_NAME}"
            "${NUTTX_COMPAT_ROOT}/arch")
    endif()

    foreach(pointer_name IN ITEMS chip board)
        resolve_nuttx_pointer_dir(NUTTX_POINTER_SOURCE
            "${NUTTX_ARCH_SOURCE}/${pointer_name}"
            "${NUTTX_ARCH_SOURCE}"
            "${NUTTX_INCLUDE_DIR}")
        file(REMOVE_RECURSE "${NUTTX_COMPAT_ROOT}/arch/${pointer_name}")
        file(COPY "${NUTTX_POINTER_SOURCE}" DESTINATION "${NUTTX_COMPAT_ROOT}/arch")
        get_filename_component(POINTER_SOURCE_NAME "${NUTTX_POINTER_SOURCE}" NAME)
        if(NOT POINTER_SOURCE_NAME STREQUAL "${pointer_name}")
            file(RENAME
                "${NUTTX_COMPAT_ROOT}/arch/${POINTER_SOURCE_NAME}"
                "${NUTTX_COMPAT_ROOT}/arch/${pointer_name}")
        endif()
    endforeach()

    set(WINDOWS_NUTTX_COMPAT_INCLUDE_DIR "${NUTTX_COMPAT_ROOT}")
endif()

set(PLATFORM_INCLUDE_DIRS
    "${WINDOWS_NUTTX_COMPAT_INCLUDE_DIR}"
    "${JY_APP_OS_SDK_OVERRIDE_DIR}"
    "${PLATFORM_DIR}"
    "${JY_APP_OS_SDK_FLOATAIR_DIR}"
    "${PLATFORM_DIR}/common"
    "${NUTTX_INCLUDE_DIR}"
    "${NUTTX_INCLUDE_DIR}/include"
    "${NUTTX_INCLUDE_DIR}/fs/rpmsgfs"
    "${JY_APP_OS_SDK_VENDOR_DIR}"
)

set(PLATFORM_SOURCES
    "${PLATFORM_DIR}/main.c"
    "${PLATFORM_DIR}/sys_adapter.c"
    "${PLATFORM_DIR}/floatair_fs.c"
    "${PLATFORM_DIR}/floatair_lcd.c"
)

set(PLATFORM_COMPILE_OPTIONS
    -c
    -fmessage-length=0
    -fno-aggressive-loop-optimizations
    -fno-common
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-isolate-erroneous-paths-dereference
    -fno-strict-aliasing
    -fno-tree-loop-distribute-patterns
    -fno-tree-switch-conversion
    -fomit-frame-pointer
    -fsigned-char
    -fsingle-precision-constant
    -fno-builtin
    -Wall
    -Wextra
    -Wstrict-prototypes
    -Wshadow
    -Wundef
    -Wdouble-promotion
    -Werror=date-time
    -Werror=implicit-int
    -Wfloat-conversion
    -Wimplicit-fallthrough
    -Wlogical-op
    -Wno-trigraphs
    -Os
	-fno-strict-aliasing
    -fno-strength-reduce
    -fomit-frame-pointer
    -mlong-calls              # 【关键】确保 ELF 程序的长跳转兼容性
    -fno-common               # 【关键】避免全局变量合并问题
    --specs=nano.specs
    -march=armv8.1-m.main+mve.fp+fp.dp
    -mfloat-abi=hard
    -mfpu=auto
    -MP
    -MT
    -mthumb
)

set(PLATFORM_LINK_OPTIONS
    -nostdlib
    -r
    -e
    main
    -Wl,--gc-sections
)

set(PLATFORM_COMPILE_DEFS "")
if(EXISTS "${PROJECT_ROOT}/.config")
    file(STRINGS "${PROJECT_ROOT}/.config" DEFCONFIG_LINES)
    foreach(line IN LISTS DEFCONFIG_LINES)
        # 过滤注释行、空行和无等号的行
        string(STRIP "${line}" line_stripped)
        if(NOT line_stripped OR line_stripped MATCHES "^#" OR NOT line_stripped MATCHES "=")
            continue()
        endif()

        # 拆分键值对（修复 CMake 算术表达式错误）
        string(FIND "${line_stripped}" "=" EQ_POS)
        # CMake 正确的算术运算方式：math(EXPR)
        math(EXPR VALUE_START_POS "${EQ_POS} + 1")
        # 拆分 KEY：从 0 到 EQ_POS 的子串
        string(SUBSTRING "${line_stripped}" 0 ${EQ_POS} KEY)
        # 拆分 VALUE：从 VALUE_START_POS 到末尾（长度-1 表示取到最后）
        string(SUBSTRING "${line_stripped}" ${VALUE_START_POS} -1 VALUE)

        string(STRIP "${KEY}" KEY)
        string(STRIP "${VALUE}" VALUE)

	    # 空字符串剥离引号
        if(VALUE MATCHES ^\"\"$)
            string(REGEX REPLACE "^\"(.*)\"$" "\\1" VALUE "${VALUE}")
        endif()
	    # 转换 y/n 为 1/0，保留其他值原样
        if(VALUE STREQUAL "y")
            set(VALUE "1")
        elseif(VALUE STREQUAL "n")
            set(VALUE "0")
        endif()
        message(STATUS "Loaded .config def: ${KEY}=${VALUE}")
	    # Check for truly empty values that would cause issues
        if("${VALUE}" STREQUAL "" AND NOT "${line_stripped}" MATCHES "=")
            message(FATAL_ERROR "Invalid value for ${KEY}")
        else()
            list(APPEND PLATFORM_COMPILE_DEFS "${KEY}=${VALUE}")
            set(${KEY} "${VALUE}")
        endif()
    endforeach()
else()
    message(FATAL_ERROR ".config file missing at ${PROJECT_ROOT}/.config")
endif()

# Conditional sources based on .config
if(CONFIG_RPMSG_TTF_CLIENT)
    list(APPEND PLATFORM_SOURCES "${PLATFORM_DIR}/rpmsgttf_client.c")
endif()
