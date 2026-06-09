cmake_minimum_required(VERSION 3.13)

set(SDL2_FOUND TRUE)

get_filename_component(_sdl2_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SDL2_INCLUDE_DIR "${_sdl2_root}/include/SDL2")
set(SDL2_INCLUDE_DIRS "${_sdl2_root}/include;${SDL2_INCLUDE_DIR}")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_sdl2_arch_dir "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_sdl2_arch_dir "x86")
else()
    set(SDL2_FOUND FALSE)
    message(FATAL_ERROR "Bundled SDL2 supports only 32-bit and 64-bit Windows builds.")
endif()

if(MINGW)
    set(_sdl2_lib_dir "${_sdl2_root}/lib/mingw/${_sdl2_arch_dir}")
    set(_sdl2_import_lib "${_sdl2_lib_dir}/libSDL2.dll.a")
elseif(MSVC OR (CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_LINKER MATCHES "lld-link"))
    set(_sdl2_lib_dir "${_sdl2_root}/lib/msvc/${_sdl2_arch_dir}")
    set(_sdl2_import_lib "${_sdl2_lib_dir}/SDL2.lib")
else()
    set(SDL2_FOUND FALSE)
    message(FATAL_ERROR "Bundled SDL2 supports only MinGW and MSVC on Windows.")
endif()

set(_sdl2_dll "${_sdl2_lib_dir}/SDL2.dll")

foreach(_sdl2_required_file IN ITEMS "${_sdl2_import_lib}" "${_sdl2_dll}")
    if(NOT EXISTS "${_sdl2_required_file}")
        set(SDL2_FOUND FALSE)
        message(FATAL_ERROR "Bundled SDL2 file is missing: ${_sdl2_required_file}")
    endif()
endforeach()

if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 SHARED IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
        IMPORTED_IMPLIB "${_sdl2_import_lib}"
        IMPORTED_LOCATION "${_sdl2_dll}"
        COMPATIBLE_INTERFACE_STRING "SDL_VERSION"
        INTERFACE_SDL_VERSION "SDL2"
    )
endif()

set(SDL2_LIBRARIES SDL2::SDL2)
set(SDL2_SDL2_FOUND TRUE)
set(SDL2_SDL2-static_FOUND FALSE)

unset(_sdl2_dll)
unset(_sdl2_import_lib)
unset(_sdl2_lib_dir)
unset(_sdl2_arch_dir)
unset(_sdl2_root)
