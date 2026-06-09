include_guard(GLOBAL)

function(jy_app_configure_arm_target target_name)
    target_compile_options(${target_name} PRIVATE ${PLATFORM_COMPILE_OPTIONS})
    target_link_options(${target_name} PRIVATE ${PLATFORM_LINK_OPTIONS})

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/romfs_staging"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_ROOT}/romfs"
                "${CMAKE_CURRENT_BINARY_DIR}/romfs_staging"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_ROOT}/scripts/fs_img.py"
                --source "$<TARGET_FILE:${target_name}>"
                --romfs-dir "${CMAKE_CURRENT_BINARY_DIR}/romfs_staging"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        VERBATIM
    )
endfunction()

function(jy_app_configure_simulator_target target_name)
    target_compile_options(${target_name} PRIVATE ${PLATFORM_COMPILE_OPTIONS})
    target_link_options(${target_name} PRIVATE ${PLATFORM_LINK_OPTIONS})

    if(SIMULATOR_USE_PKG_CONFIG_SDL2)
        find_package(PkgConfig QUIET)
        if(NOT PkgConfig_FOUND)
            message(FATAL_ERROR "pkg-config is required for the Linux x86 SDL2 build.")
        endif()

        set(_saved_pkg_config_libdir "$ENV{PKG_CONFIG_LIBDIR}")
        if(LINUX_X86_SDL2_PATH)
            set(ENV{PKG_CONFIG_LIBDIR} "${LINUX_X86_SDL2_PATH}")
        endif()
        pkg_check_modules(SIMULATOR_PKG_SDL2 REQUIRED sdl2)
        set(ENV{PKG_CONFIG_LIBDIR} "${_saved_pkg_config_libdir}")
        unset(_saved_pkg_config_libdir)

        target_include_directories(${target_name} PRIVATE ${SIMULATOR_PKG_SDL2_INCLUDE_DIRS})
        target_compile_options(${target_name} PRIVATE ${SIMULATOR_PKG_SDL2_CFLAGS_OTHER})
        set(SIMULATOR_SDL_LIBS ${SIMULATOR_PKG_SDL2_LDFLAGS})
    else()
        find_package(SDL2 REQUIRED)
        target_include_directories(${target_name} PRIVATE ${SDL2_INCLUDE_DIRS})
        set(SIMULATOR_SDL_LIBS ${SDL2_LIBRARIES})
        if(TARGET SDL2::SDL2)
            set(SIMULATOR_SDL_LIBS SDL2::SDL2)
        endif()
    endif()

    set(SIMULATOR_THREAD_LIBS)
    if(NOT JY_APP_TARGET_PLATFORM STREQUAL "msvc" AND NOT SIMULATOR_USE_WIN32_THREAD_SHIM)
        find_package(Threads REQUIRED)
        set(SIMULATOR_THREAD_LIBS Threads::Threads)
    endif()

    target_link_libraries(${target_name} PRIVATE
        ${SIMULATOR_SDL_LIBS}
        ${SIMULATOR_THREAD_LIBS}
        ${PLATFORM_LINK_LIBS}
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /FImsvc_compat.h)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE -include time.h)
    endif()

    set(LV_TINY_TTF_SOURCE "${LVGL_DIR}/src/libs/tiny_ttf/lv_tiny_ttf.c")
    if(EXISTS "${LV_TINY_TTF_SOURCE}" AND
            (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang"))
        set_source_files_properties("${LV_TINY_TTF_SOURCE}" PROPERTIES COMPILE_FLAGS "-Wno-format")
    endif()

    set(LV_FS_CBFS_SOURCE "${LVGL_DIR}/src/libs/fsdrv/lv_fs_cbfs.c")
    if(EXISTS "${LV_FS_CBFS_SOURCE}" AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        set_source_files_properties("${LV_FS_CBFS_SOURCE}" PROPERTIES COMPILE_FLAGS "-Wno-unused-function")
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/jyt_d"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/romfs"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_ROOT}/lfsd"
                "${CMAKE_BINARY_DIR}/jyt_d"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_ROOT}/romfs"
                "${CMAKE_BINARY_DIR}/romfs"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_ROOT}/scripts/StringPool.py"
                --csv "${PROJECT_ROOT}/StringPool.csv"
                --json-out "${CMAKE_BINARY_DIR}/jyt_d/system/i18n"
    )

    set(SIMULATOR_INSTALL_RUNTIME_FILES)

    if(JY_APP_TARGET_PLATFORM STREQUAL "mingw")
        if(TARGET SDL2::SDL2)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:SDL2::SDL2>"
                        "$<TARGET_FILE_DIR:${target_name}>/SDL2.dll"
            )
            get_target_property(_sdl2_runtime_path SDL2::SDL2 IMPORTED_LOCATION)
            if(_sdl2_runtime_path)
                list(APPEND SIMULATOR_INSTALL_RUNTIME_FILES "${_sdl2_runtime_path}")
            endif()
            unset(_sdl2_runtime_path)
        endif()

        get_filename_component(_mingw_bin_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(_mingw_compiler_name "${CMAKE_C_COMPILER}" NAME)
        string(REGEX REPLACE "(-gcc|-clang)(\\.exe)?$" "" _mingw_target_triple "${_mingw_compiler_name}")
        set(_mingw_runtime_target_triple)
        if(_mingw_target_triple MATCHES "mingw|w64")
            set(_mingw_runtime_target_triple "${_mingw_target_triple}")
        elseif(CMAKE_C_COMPILER_TARGET AND CMAKE_C_COMPILER_TARGET MATCHES "mingw|w64")
            set(_mingw_runtime_target_triple "${CMAKE_C_COMPILER_TARGET}")
        endif()
        set(_mingw_runtime_search_dirs)
        if(_mingw_runtime_target_triple)
            list(APPEND _mingw_runtime_search_dirs
                "${_mingw_bin_dir}/../${_mingw_runtime_target_triple}/bin"
                "${_mingw_bin_dir}/../${_mingw_runtime_target_triple}/lib"
            )
        endif()
        list(APPEND _mingw_runtime_search_dirs
            "${_mingw_bin_dir}"
            "${_mingw_bin_dir}/../lib"
        )
        set(_mingw_runtime_print_file_args)
        if(CMAKE_C_COMPILER_TARGET)
            list(APPEND _mingw_runtime_print_file_args "-target" "${CMAKE_C_COMPILER_TARGET}")
            list(APPEND _mingw_runtime_search_dirs
                "/usr/${CMAKE_C_COMPILER_TARGET}/bin"
                "/usr/${CMAKE_C_COMPILER_TARGET}/lib"
                "/usr/local/${CMAKE_C_COMPILER_TARGET}/bin"
                "/usr/local/${CMAKE_C_COMPILER_TARGET}/lib"
                "/opt/homebrew/${CMAKE_C_COMPILER_TARGET}/bin"
                "/opt/homebrew/${CMAKE_C_COMPILER_TARGET}/lib"
            )
        endif()
        if(_mingw_target_triple MATCHES "mingw|w64")
            list(APPEND _mingw_runtime_search_dirs
                "/usr/${_mingw_target_triple}/bin"
                "/usr/${_mingw_target_triple}/lib"
                "/usr/local/${_mingw_target_triple}/bin"
                "/usr/local/${_mingw_target_triple}/lib"
                "/usr/local/opt/mingw-w64/bin"
                "/usr/local/opt/mingw-w64/lib"
                "/usr/local/opt/mingw-w64/toolchain-x86_64/${_mingw_target_triple}/bin"
                "/usr/local/opt/mingw-w64/toolchain-x86_64/${_mingw_target_triple}/lib"
                "/usr/local/opt/mingw-w64/toolchain-i686/${_mingw_target_triple}/bin"
                "/usr/local/opt/mingw-w64/toolchain-i686/${_mingw_target_triple}/lib"
                "/opt/homebrew/${_mingw_target_triple}/bin"
                "/opt/homebrew/${_mingw_target_triple}/lib"
                "/opt/homebrew/opt/mingw-w64/bin"
                "/opt/homebrew/opt/mingw-w64/lib"
                "/opt/homebrew/opt/mingw-w64/toolchain-x86_64/${_mingw_target_triple}/bin"
                "/opt/homebrew/opt/mingw-w64/toolchain-x86_64/${_mingw_target_triple}/lib"
                "/opt/homebrew/opt/mingw-w64/toolchain-i686/${_mingw_target_triple}/bin"
                "/opt/homebrew/opt/mingw-w64/toolchain-i686/${_mingw_target_triple}/lib"
            )
        endif()
        set(_mingw_runtime_dlls)
        if(NOT SIMULATOR_USE_WIN32_THREAD_SHIM)
            list(APPEND _mingw_runtime_dlls libwinpthread-1.dll)
        endif()
        foreach(_runtime_dll ${_mingw_runtime_dlls})
            unset(_mingw_runtime_dll_path)
            unset(_mingw_runtime_dll_path CACHE)
            find_file(_mingw_runtime_dll_path
                NAMES "${_runtime_dll}"
                PATHS ${_mingw_runtime_search_dirs}
                NO_DEFAULT_PATH
            )

            execute_process(
                COMMAND "${CMAKE_C_COMPILER}" ${_mingw_runtime_print_file_args} -print-file-name=${_runtime_dll}
                OUTPUT_VARIABLE _mingw_runtime_print_file_path
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(NOT _mingw_runtime_dll_path AND IS_ABSOLUTE "${_mingw_runtime_print_file_path}" AND EXISTS "${_mingw_runtime_print_file_path}")
                set(_mingw_runtime_dll_path "${_mingw_runtime_print_file_path}")
            endif()

            if(_mingw_runtime_dll_path)
                message(STATUS "MinGW runtime DLL ${_runtime_dll}: ${_mingw_runtime_dll_path}")
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_mingw_runtime_dll_path}"
                            "${CMAKE_BINARY_DIR}/${_runtime_dll}"
                )
                list(APPEND SIMULATOR_INSTALL_RUNTIME_FILES "${_mingw_runtime_dll_path}")
            else()
                message(WARNING "MinGW runtime DLL was not found: ${_runtime_dll}")
            endif()
            unset(_mingw_runtime_print_file_path)
            unset(_mingw_runtime_dll_path)
            unset(_mingw_runtime_dll_path CACHE)
        endforeach()
        unset(_mingw_runtime_dlls)
        unset(_mingw_runtime_print_file_args)
        unset(_mingw_runtime_search_dirs)
        unset(_mingw_runtime_target_triple)
        unset(_mingw_target_triple)
        unset(_mingw_compiler_name)
        unset(_mingw_bin_dir)
    elseif(JY_APP_TARGET_PLATFORM STREQUAL "msvc")
        if(TARGET SDL2::SDL2)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:SDL2::SDL2>"
                        "$<TARGET_FILE_DIR:${target_name}>/SDL2.dll"
            )
            get_target_property(_sdl2_runtime_path SDL2::SDL2 IMPORTED_LOCATION)
            if(_sdl2_runtime_path)
                list(APPEND SIMULATOR_INSTALL_RUNTIME_FILES "${_sdl2_runtime_path}")
            endif()
            unset(_sdl2_runtime_path)
        endif()
    endif()

    install(TARGETS ${target_name}
        RUNTIME DESTINATION "."
    )
    install(DIRECTORY "${CMAKE_BINARY_DIR}/jyt_d/"
        DESTINATION "jyt_d"
    )
    install(DIRECTORY "${CMAKE_BINARY_DIR}/romfs/"
        DESTINATION "romfs"
    )
    install(FILES "${SIMULATOR_DIR}/simulator_socket.conf"
        DESTINATION "."
    )
    install(PROGRAMS "${SIMULATOR_DIR}/simulator_event_panel.py"
        DESTINATION "."
    )
    if(SIMULATOR_INSTALL_RUNTIME_FILES)
        install(FILES ${SIMULATOR_INSTALL_RUNTIME_FILES}
            DESTINATION "."
        )
    endif()
endfunction()
