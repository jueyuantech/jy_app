include_guard(GLOBAL)

# 功能：校验当前目标平台的编译器与位宽约束。
# 参数：TARGET_PLATFORM，目标平台标识，可为 linux/macos/mingw/msvc/arm。
# 返回：无；校验失败时通过 message(FATAL_ERROR) 终止配置。
function(jy_app_validate_target_toolchain TARGET_PLATFORM)
    if(TARGET_PLATFORM STREQUAL "linux")
        if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR
                "The Linux simulator supports GCC or Clang. "
                "Configure with -DCMAKE_C_COMPILER=gcc or clang.")
        endif()
    elseif(TARGET_PLATFORM STREQUAL "macos")
        if(NOT APPLE)
            message(FATAL_ERROR
                "The macOS simulator platform must be configured on macOS.")
        endif()

        if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR
                "The macOS simulator supports AppleClang or Clang. "
                "Configure with -DCMAKE_C_COMPILER=clang.")
        endif()

        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            message(FATAL_ERROR
                "The macOS simulator platform supports 64-bit builds only.")
        endif()
    elseif(TARGET_PLATFORM STREQUAL "mingw")
        if(NOT MINGW)
            message(FATAL_ERROR
                "The MinGW simulator platform requires a MinGW compiler.")
        endif()

        if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR
                "The MinGW simulator requires a MinGW GCC or Clang toolchain. "
                "Configure with gcc, i686-w64-mingw32-gcc, x86_64-w64-mingw32-gcc, "
                "or a compatible MinGW Clang compiler.")
        endif()

        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            message(FATAL_ERROR
                "The MinGW simulator platform supports only 32-bit and 64-bit Windows builds.")
        endif()
    elseif(TARGET_PLATFORM STREQUAL "msvc")
        if(NOT MSVC AND NOT (CMAKE_C_COMPILER_ID MATCHES "Clang" AND
                CMAKE_LINKER MATCHES "lld-link"))
            message(FATAL_ERROR
                "The MSVC simulator platform requires an MSVC ABI compiler "
                "(cl, Visual Studio clang, or clang-cl).")
        endif()

        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            message(FATAL_ERROR
                "The MSVC simulator platform supports only 32-bit and 64-bit Windows builds.")
        endif()
    elseif(TARGET_PLATFORM STREQUAL "arm")
        if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
            message(FATAL_ERROR
                "The ARM build requires arm-none-eabi-gcc. "
                "Clang is not supported.")
        endif()
    else()
        message(FATAL_ERROR
            "Unsupported target platform: ${TARGET_PLATFORM}.")
    endif()
endfunction()

# 功能：根据显式传入的编译器名称和当前运行平台推断目标平台。
# 参数：OUT_VAR，用于接收推断结果的输出变量名。
# 返回：通过 PARENT_SCOPE 回写 OUT_VAR，结果为 linux/macos/mingw/msvc/arm 之一。
function(jy_app_detect_target_platform OUT_VAR)
    set(_compiler_candidates)

    if(DEFINED CMAKE_C_COMPILER AND NOT CMAKE_C_COMPILER STREQUAL "")
        list(APPEND _compiler_candidates "${CMAKE_C_COMPILER}")
    endif()

    if(DEFINED ENV{CC} AND NOT "$ENV{CC}" STREQUAL "")
        list(APPEND _compiler_candidates "$ENV{CC}")
    endif()

    foreach(_compiler IN LISTS _compiler_candidates)
        get_filename_component(_compiler_name "${_compiler}" NAME)
        string(TOLOWER "${_compiler_name}" _compiler_name_lower)
        string(REGEX REPLACE "\\.exe$" "" _compiler_name_key "${_compiler_name_lower}")

        if(_compiler_name_key STREQUAL "arm-none-eabi-gcc")
            set(${OUT_VAR} "arm" PARENT_SCOPE)
            return()
        endif()

        if(_compiler_name_key MATCHES "mingw|w64-mingw32")
            set(${OUT_VAR} "mingw" PARENT_SCOPE)
            return()
        endif()

        if(_compiler_name_key STREQUAL "cl")
            set(${OUT_VAR} "msvc" PARENT_SCOPE)
            return()
        endif()

        if(DEFINED CMAKE_C_COMPILER_TARGET AND
                CMAKE_C_COMPILER_TARGET MATCHES "mingw|windows-gnu")
            set(${OUT_VAR} "mingw" PARENT_SCOPE)
            return()
        endif()

        if(DEFINED CMAKE_C_COMPILER_TARGET AND
                CMAKE_C_COMPILER_TARGET MATCHES "msvc|windows-msvc")
            set(${OUT_VAR} "msvc" PARENT_SCOPE)
            return()
        endif()

        if(_compiler_name_key STREQUAL "clang-cl")
            if(CMAKE_HOST_WIN32)
                set(${OUT_VAR} "msvc" PARENT_SCOPE)
            else()
                message(FATAL_ERROR
                    "clang-cl uses MSVC-style command-line parsing and cannot "
                    "build the native ${CMAKE_HOST_SYSTEM_NAME} simulator. "
                    "Configure with clang for macOS/Linux, or use clang-cl only "
                    "for the Windows MSVC simulator target.")
            endif()
            return()
        endif()

        if(_compiler_name_key STREQUAL "clang")
            if(CMAKE_HOST_WIN32)
                get_filename_component(_compiler_dir "${_compiler}" DIRECTORY)
                string(TOLOWER "${_compiler_dir}" _compiler_dir_lower)
                if(_compiler_dir_lower MATCHES "/vc/tools/llvm/bin$" OR
                        EXISTS "${_compiler_dir}/lld-link.exe")
                    set(${OUT_VAR} "msvc" PARENT_SCOPE)
                else()
                    set(${OUT_VAR} "mingw" PARENT_SCOPE)
                endif()
            elseif(CMAKE_HOST_APPLE)
                set(${OUT_VAR} "macos" PARENT_SCOPE)
            else()
                set(${OUT_VAR} "linux" PARENT_SCOPE)
            endif()
            return()
        endif()

        if(_compiler_name_key STREQUAL "gcc" OR _compiler_name_key STREQUAL "cc")
            if(CMAKE_HOST_WIN32)
                set(${OUT_VAR} "mingw" PARENT_SCOPE)
            elseif(CMAKE_HOST_APPLE)
                set(${OUT_VAR} "macos" PARENT_SCOPE)
            else()
                set(${OUT_VAR} "linux" PARENT_SCOPE)
            endif()
            return()
        endif()
    endforeach()

    if(_compiler_candidates)
        message(FATAL_ERROR
            "Failed to detect target platform from the specified compiler. "
            "Please use arm-none-eabi-gcc for ARM, i686-w64-mingw32-gcc for MinGW, "
            "cl/clang/clang-cl for MSVC, gcc/clang for the Linux simulator, "
            "or clang for the macOS simulator.")
    endif()

    find_program(_jy_app_arm_gcc arm-none-eabi-gcc)
    if(_jy_app_arm_gcc)
        set(${OUT_VAR} "arm" PARENT_SCOPE)
    else()
        message(FATAL_ERROR
            "No compiler was specified and arm-none-eabi-gcc was not found in PATH. "
            "Specify CMAKE_C_COMPILER explicitly for linux/macos/mingw/msvc builds, "
            "or install arm-none-eabi-gcc for the ARM build.")
    endif()
endfunction()

function(jy_app_prepare_windows_mingw_platform)
    if(NOT CMAKE_HOST_WIN32)
        set(CMAKE_SYSTEM_NAME Windows CACHE STRING "Cross-compilation target system" FORCE)
    endif()
endfunction()

function(jy_app_prepare_windows_msvc_platform)
    # Visual Studio tools can localize cl.exe diagnostics. Keep them English so
    # CMake/Ninja showIncludes parsing stays stable; MinGW/Linux do not need it.
    set(ENV{VSLANG} "1033")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY PARENT_SCOPE)

    get_filename_component(_jy_app_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(_jy_app_llvm_rc llvm-rc.exe
        HINTS "${_jy_app_compiler_dir}"
        DOC "LLVM resource compiler used by CMake's Windows-Clang platform checks"
    )

    # This project has no .rc resources. CMake's Windows-Clang platform module
    # still probes RC internally, so use clang's llvm-rc when it is available
    # instead of requiring Windows Kits tools to be in PATH.
    if(_jy_app_llvm_rc)
        set(ENV{RC} "${_jy_app_llvm_rc}")
        set(CMAKE_RC_COMPILER_INIT "${_jy_app_llvm_rc}" PARENT_SCOPE)
        set(CMAKE_RC_COMPILER "${_jy_app_llvm_rc}" CACHE FILEPATH
            "Resource compiler used by CMake's Windows-Clang platform checks" FORCE)
    endif()
endfunction()
