foreach(_flag_var CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
    if(DEFINED ${_flag_var})
        string(REPLACE "/MDd" "/MD" _runtime_flags "${${_flag_var}}")
        set(${_flag_var} "${_runtime_flags}" CACHE STRING "MSVC Debug flags" FORCE)
    endif()
endforeach()
unset(_runtime_flags)

include(SimulatorCommon)

jy_app_prepare_simulator_common()


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

set_property(SOURCE "${SIMULATOR_DIR}/lv_port_fs.c" APPEND PROPERTY
    COMPILE_DEFINITIONS FLOATAIR_SIMULATOR_LV_FS_CACHE_SIZE=262144)

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
