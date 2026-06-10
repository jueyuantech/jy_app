include(SimulatorCommon)

jy_app_prepare_simulator_common()

set(ARCH "x86" CACHE STRING "Linux simulator architecture: x86 or amd64")
set_property(CACHE ARCH PROPERTY STRINGS x86 amd64)
string(TOLOWER "${ARCH}" _simulator_linux_arch)

if(_simulator_linux_arch STREQUAL "i386" OR _simulator_linux_arch STREQUAL "i686" OR
        _simulator_linux_arch STREQUAL "x86_32" OR _simulator_linux_arch STREQUAL "32")
    set(_simulator_linux_arch "x86")
elseif(_simulator_linux_arch STREQUAL "x64" OR _simulator_linux_arch STREQUAL "x86_64" OR
        _simulator_linux_arch STREQUAL "64")
    set(_simulator_linux_arch "amd64")
endif()

if(_simulator_linux_arch STREQUAL "x86")
    set(SIMULATOR_USE_PKG_CONFIG_SDL2 TRUE)
    set(_simulator_linux_compile_arch_options -m32 -msse2)
    set(_simulator_linux_link_arch_options -m32)
    set(_simulator_linux_default_sdl2_pkg_config_libdir
        "/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig")
elseif(_simulator_linux_arch STREQUAL "amd64")
    set(SIMULATOR_USE_PKG_CONFIG_SDL2 FALSE)
    set(_simulator_linux_compile_arch_options)
    set(_simulator_linux_link_arch_options)
    set(_simulator_linux_default_sdl2_pkg_config_libdir "")
else()
    message(FATAL_ERROR
        "Unsupported ARCH: ${ARCH}. "
        "Use x86 or amd64.")
endif()

set(LINUX_X86_SDL2_PATH
    "${_simulator_linux_default_sdl2_pkg_config_libdir}"
    CACHE STRING "SDL2 pkg-config search path used for the Linux x86 simulator build"
)

set(PLATFORM_COMPILE_DEFS
    ${JY_APP_SIMULATOR_COMMON_COMPILE_DEFS}
    LV_USE_OS=LV_OS_PTHREAD
    LV_USE_FS_WIN32=0
    LV_USE_FS_POSIX=1
    LV_FS_POSIX_LETTER='A'
    LV_FS_POSIX_PATH="."
)

set(PLATFORM_COMPILE_OPTIONS
    ${_simulator_linux_compile_arch_options}
)

set(PLATFORM_LINK_OPTIONS
    ${_simulator_linux_link_arch_options}
)

set(PLATFORM_LINK_LIBS
    m
)

unset(_simulator_linux_compile_arch_options)
unset(_simulator_linux_link_arch_options)
unset(_simulator_linux_default_sdl2_pkg_config_libdir)
unset(_simulator_linux_arch)
