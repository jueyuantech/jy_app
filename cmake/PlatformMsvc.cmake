foreach(_flag_var CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
    if(DEFINED ${_flag_var})
        string(REPLACE "/MDd" "/MD" _runtime_flags "${${_flag_var}}")
        set(${_flag_var} "${_runtime_flags}" CACHE STRING "MSVC Debug flags" FORCE)
    endif()
endforeach()
unset(_runtime_flags)

include(SimulatorCommon)

jy_app_prepare_simulator_common()

set(SIMULATOR_SDL2_ROOT "" CACHE PATH "Root directory of the SDL2 VC SDK used by the MSVC simulator build")
set(_default_sdl2_root "${SIMULATOR_DIR}/windows/SDL2")
if(NOT SIMULATOR_SDL2_ROOT OR NOT EXISTS "${SIMULATOR_SDL2_ROOT}/cmake/sdl2-config.cmake")
    if(EXISTS "${_default_sdl2_root}/cmake/sdl2-config.cmake")
        set(SIMULATOR_SDL2_ROOT "${_default_sdl2_root}" CACHE PATH "Root directory of the SDL2 VC SDK used by the MSVC simulator build" FORCE)
    endif()
endif()

if(SIMULATOR_SDL2_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${SIMULATOR_SDL2_ROOT}")
    set(SDL2_DIR "${SIMULATOR_SDL2_ROOT}/cmake" CACHE PATH "Directory containing the bundled SDL2 CMake config" FORCE)
endif()

list(PREPEND PLATFORM_INCLUDE_DIRS "${SIMULATOR_DIR}/windows/win32/include")

list(APPEND PLATFORM_SOURCES "${SIMULATOR_DIR}/windows/win32/simulator_lvgl_osal_windows.c")

set(PLATFORM_COMPILE_DEFS
    ${JY_APP_SIMULATOR_COMMON_COMPILE_DEFS}
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
    LV_USE_OS=LV_OS_CUSTOM
    LV_OS_CUSTOM_INCLUDE="simulator_lvgl_osal_windows.h"
    LV_USE_FS_WIN32=0
    LV_USE_FS_POSIX=0
)

if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(PLATFORM_COMPILE_OPTIONS
        /utf-8
        /D__thread=__declspec\(thread\)
    )
    set(PLATFORM_LINK_OPTIONS /MANIFEST:NO)
else()
    set(PLATFORM_COMPILE_OPTIONS
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
        "-D__thread=__declspec(thread)"
    )
    set(PLATFORM_LINK_OPTIONS
        -Xlinker
        /MANIFEST:NO
    )
endif()
set(PLATFORM_LINK_LIBS ws2_32)
