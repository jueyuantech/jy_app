include(SimulatorCommon)

jy_app_prepare_simulator_common()

set(SIMULATOR_SDL2_ROOT "" CACHE PATH "Root directory of the SDL2 SDK used by the MinGW simulator build")
set(SIMULATOR_MINGW_THREAD_BACKEND "auto" CACHE STRING "Thread backend used by the MinGW simulator build (auto, pthread, win32)")
set_property(CACHE SIMULATOR_MINGW_THREAD_BACKEND PROPERTY STRINGS auto pthread win32)

set(SIMULATOR_USE_WIN32_THREAD_SHIM FALSE)
if(SIMULATOR_MINGW_THREAD_BACKEND STREQUAL "win32")
    set(SIMULATOR_USE_WIN32_THREAD_SHIM TRUE)
elseif(SIMULATOR_MINGW_THREAD_BACKEND STREQUAL "auto")
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -v
        OUTPUT_VARIABLE _mingw_compiler_version_out
        ERROR_VARIABLE _mingw_compiler_version_err
        RESULT_VARIABLE _mingw_compiler_version_result
    )
    set(_mingw_compiler_version_text "${_mingw_compiler_version_out}\n${_mingw_compiler_version_err}")
    if(_mingw_compiler_version_text MATCHES "Thread model:[ ]*win32")
        set(SIMULATOR_USE_WIN32_THREAD_SHIM TRUE)
    endif()
    unset(_mingw_compiler_version_text)
    unset(_mingw_compiler_version_result)
    unset(_mingw_compiler_version_err)
    unset(_mingw_compiler_version_out)
elseif(NOT SIMULATOR_MINGW_THREAD_BACKEND STREQUAL "pthread")
    message(FATAL_ERROR "SIMULATOR_MINGW_THREAD_BACKEND must be one of: auto, pthread, win32")
endif()

if(SIMULATOR_USE_WIN32_THREAD_SHIM)
    message(STATUS "MinGW simulator thread backend: win32 shim")
    set(_mingw_lv_os_defs
        LV_USE_OS=LV_OS_CUSTOM
        LV_OS_CUSTOM_INCLUDE="simulator_lvgl_osal_windows.h"
    )
else()
    message(STATUS "MinGW simulator thread backend: pthread")
    set(_mingw_lv_os_defs LV_USE_OS=LV_OS_PTHREAD)
endif()

set(_default_sdl2_root "${SIMULATOR_DIR}/windows/SDL2")
if(NOT SIMULATOR_SDL2_ROOT OR NOT EXISTS "${SIMULATOR_SDL2_ROOT}/cmake/sdl2-config.cmake")
    if(EXISTS "${_default_sdl2_root}/cmake/sdl2-config.cmake")
        set(SIMULATOR_SDL2_ROOT "${_default_sdl2_root}" CACHE PATH "Root directory of the SDL2 SDK used by the MinGW simulator build" FORCE)
    endif()
endif()

if(SIMULATOR_SDL2_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${SIMULATOR_SDL2_ROOT}")
    set(SDL2_DIR "${SIMULATOR_SDL2_ROOT}/cmake" CACHE PATH "Directory containing the bundled SDL2 CMake config" FORCE)
endif()

if(SIMULATOR_USE_WIN32_THREAD_SHIM)
    list(PREPEND PLATFORM_INCLUDE_DIRS "${SIMULATOR_DIR}/windows/win32/include")
endif()

if(SIMULATOR_USE_WIN32_THREAD_SHIM)
    list(APPEND PLATFORM_SOURCES "${SIMULATOR_DIR}/windows/win32/simulator_lvgl_osal_windows.c")
endif()

set(PLATFORM_COMPILE_DEFS
    ${JY_APP_SIMULATOR_COMMON_COMPILE_DEFS}
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0600
    ${_mingw_lv_os_defs}
    LV_USE_FS_WIN32=0
    LV_USE_FS_POSIX=0
)

if(SIMULATOR_USE_WIN32_THREAD_SHIM)
    list(APPEND PLATFORM_COMPILE_DEFS FLOATAIR_USE_WIN32_PTHREAD_SHIM)
endif()

set(PLATFORM_COMPILE_OPTIONS)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(_mingw_clang_i686_target FALSE)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_mingw_clang_i686_target TRUE)
    endif()
    if(DEFINED CMAKE_C_COMPILER_TARGET AND
            CMAKE_C_COMPILER_TARGET MATCHES "^(i[3-6]86|x86)(-|_)")
        set(_mingw_clang_i686_target TRUE)
    endif()
    if(_mingw_clang_i686_target)
        list(APPEND PLATFORM_COMPILE_OPTIONS -msse2)
    endif()
    unset(_mingw_clang_i686_target)
endif()
set(PLATFORM_LINK_OPTIONS)
set(PLATFORM_LINK_LIBS ws2_32)
