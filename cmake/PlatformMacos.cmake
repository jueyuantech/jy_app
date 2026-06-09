include(SimulatorCommon)

jy_app_prepare_simulator_common()

set(SIMULATOR_SDL2_ROOT "" CACHE PATH "Root directory of the SDL2 SDK used by the macOS simulator build")
if(SIMULATOR_SDL2_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${SIMULATOR_SDL2_ROOT}")
endif()

foreach(_brew_prefix /opt/homebrew /usr/local)
    if(EXISTS "${_brew_prefix}")
        list(PREPEND CMAKE_PREFIX_PATH "${_brew_prefix}")
    endif()
endforeach()
unset(_brew_prefix)

set(PLATFORM_COMPILE_DEFS
    ${JY_APP_SIMULATOR_COMMON_COMPILE_DEFS}
    LV_USE_OS=LV_OS_PTHREAD
    LV_USE_FS_WIN32=0
    LV_USE_FS_POSIX=1
    LV_FS_POSIX_LETTER='A'
    LV_FS_POSIX_PATH="."
)

set(PLATFORM_COMPILE_OPTIONS)
set(PLATFORM_LINK_OPTIONS)
set(PLATFORM_LINK_LIBS m)
