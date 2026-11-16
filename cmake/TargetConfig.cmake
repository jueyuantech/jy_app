include_guard(GLOBAL)

function(jy_app_configure_arm_target target_name)
    get_filename_component(_arm_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(JY_APP_OBJCOPY NAMES "${TOOLCHAIN_PREFIX}objcopy" HINTS "${_arm_bin}")
    if(NOT JY_APP_OBJCOPY)
        message(FATAL_ERROR "ARM objcopy is required to create the device ELF")
    endif()
    target_compile_options(${target_name} PRIVATE ${PLATFORM_COMPILE_OPTIONS})
    target_link_options(${target_name} PRIVATE ${PLATFORM_LINK_OPTIONS})

    if(NOT IS_DIRECTORY "${JY_APP_LEFT_ROMFS_DIR}")
        message(FATAL_ERROR "Left ROMFS source directory not found: ${JY_APP_LEFT_ROMFS_DIR}")
    endif()

    set(_left_kws_firmware "${JY_APP_LEFT_ROMFS_DIR}/kws/kws_firmware.bin")
    set(_left_kws_model "${JY_APP_LEFT_ROMFS_DIR}/kws/kws_model.bin")
    foreach(_left_kws_file IN ITEMS "${_left_kws_firmware}" "${_left_kws_model}")
        if(NOT EXISTS "${_left_kws_file}")
            message(FATAL_ERROR "Required left ROMFS KWS file not found: ${_left_kws_file}")
        endif()
    endforeach()

    set(_font_md5_script "${PROJECT_ROOT}/scripts/write_font_md5.py")
    set(_bitmap_font_script "${PROJECT_ROOT}/scripts/generate_bitmap_font.py")
    set(_staging_font_dir "${CMAKE_CURRENT_BINARY_DIR}/romfs_staging/system/font")

    # Regenerate filesystem images when the packer, fonts, hash generator, or KWS resources change.
    set(_font_link_depends
        "${PROJECT_ROOT}/scripts/fs_img.py"
        "${_left_kws_firmware}"
        "${_left_kws_model}"
        "${PROJECT_ROOT}/romfs/system/font/font.ttf"
        "${_font_md5_script}"
    )
    if(JY_APP_FONT_TYPE STREQUAL "bitmap")
        list(APPEND _font_link_depends "${_bitmap_font_script}")
    endif()
    set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS ${_font_link_depends})

    if(JY_APP_FONT_TYPE STREQUAL "bitmap")
        # 单基准字号点阵字库：生成 font.bin，运行时按目标字号缩放渲染。
        set(_font_staging_file "${_staging_font_dir}/font.bin")
        set(_font_prepare_cmds
            COMMAND "${Python3_EXECUTABLE}" "${_bitmap_font_script}"
                    --font "${PROJECT_ROOT}/romfs/system/font/font.ttf"
                    --output "${_font_staging_file}"
                    --size "${JY_APP_BITMAP_FONT_SIZE}"
                    --bpp "${JY_APP_BITMAP_BPP}"
        )
        set(_font_md5_args --font "${_font_staging_file}")
    else()
        set(_font_staging_file "${_staging_font_dir}/font.ttf")
        set(_font_prepare_cmds
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${PROJECT_ROOT}/romfs/system/font/font.ttf"
                    "${_font_staging_file}"
        )
        set(_font_md5_args --font "${_font_staging_file}")
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/romfs_staging"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/left_romfs_staging"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_staging_font_dir}"
        ${_font_prepare_cmds}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${JY_APP_LEFT_ROMFS_DIR}"
                "${CMAKE_CURRENT_BINARY_DIR}/left_romfs_staging"
        COMMAND "${Python3_EXECUTABLE}" "${_font_md5_script}"
                ${_font_md5_args}
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_ROOT}/scripts/fs_img.py"
                --source "$<TARGET_FILE:${target_name}>"
                --objcopy "${JY_APP_OBJCOPY}"
                --app-version "${JY_APP_VERSION_STRING}"
                --romfs-dir "${CMAKE_CURRENT_BINARY_DIR}/romfs_staging"
                --left-romfs-dir "${CMAKE_CURRENT_BINARY_DIR}/left_romfs_staging"
                --symbol-file "${JY_APP_OS_SDK_FLOATAIR_DIR}/SymbolTable.def"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        VERBATIM
    )
endfunction()

function(jy_app_configure_simulator_target target_name)

    set(_font_md5_script "${PROJECT_ROOT}/scripts/write_font_md5.py")
    set(_bitmap_font_script "${PROJECT_ROOT}/scripts/generate_bitmap_font.py")
    set(_sim_font_dir "${CMAKE_BINARY_DIR}/romfs/system/font")

    set(_sim_font_depends
        "${PROJECT_ROOT}/romfs/system/font/font.ttf"
        "${_font_md5_script}"
    )
    if(JY_APP_FONT_TYPE STREQUAL "bitmap")
        list(APPEND _sim_font_depends "${_bitmap_font_script}")
    endif()
    set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS ${_sim_font_depends})

    if(JY_APP_FONT_TYPE STREQUAL "bitmap")
        # 单基准字号点阵字库：生成 font.bin，运行时按目标字号缩放渲染。
        set(_sim_font_file "${_sim_font_dir}/font.bin")
        set(_sim_font_prepare_cmds
            COMMAND "${Python3_EXECUTABLE}" "${_bitmap_font_script}"
                    --font "${PROJECT_ROOT}/romfs/system/font/font.ttf"
                    --output "${_sim_font_file}"
                    --size "${JY_APP_BITMAP_FONT_SIZE}"
                    --bpp "${JY_APP_BITMAP_BPP}"
        )
        set(_sim_font_md5_args --font "${_sim_font_file}")
    else()
        set(_sim_font_file "${_sim_font_dir}/font.ttf")
        set(_sim_font_prepare_cmds
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${PROJECT_ROOT}/romfs/system/font/font.ttf"
                    "${_sim_font_file}"
        )
        set(_sim_font_md5_args --font "${_sim_font_file}")
    endif()

    target_compile_options(${target_name} PRIVATE ${PLATFORM_COMPILE_OPTIONS})
    target_link_options(${target_name} PRIVATE ${PLATFORM_LINK_OPTIONS})

    # 三端模拟器统一从固定版本源码构建 SDL2 静态库。
    set(SDL_SHARED OFF CACHE BOOL "Build SDL2 shared library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Build SDL2 static library" FORCE)
    set(SDL_TEST OFF CACHE BOOL "Build SDL2 test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Build SDL2 tests" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "Disable SDL2 SDK installation" FORCE)
    if(JY_APP_TARGET_PLATFORM STREQUAL "linux" AND "-m32" IN_LIST PLATFORM_COMPILE_OPTIONS)
        set(_saved_pkg_config_libdir "$ENV{PKG_CONFIG_LIBDIR}")
        set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig")
    endif()
    add_subdirectory("${PROJECT_ROOT}/thirdparty/SDL2"
                     "${CMAKE_BINARY_DIR}/thirdparty/SDL2" EXCLUDE_FROM_ALL)
    if(JY_APP_TARGET_PLATFORM STREQUAL "linux")
        # SDL 静态库需兼容 Linux 默认的 PIE 可执行文件链接。
        set_target_properties(SDL2-static PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    if(JY_APP_TARGET_PLATFORM STREQUAL "macos")
        # SDL2 的旧 macOS API 弃用提示仅在第三方目标中关闭。
        target_compile_options(SDL2-static PRIVATE -Wno-deprecated-declarations)
    endif()
    if(JY_APP_TARGET_PLATFORM STREQUAL "msvc" AND
       CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # SDL2 上游源码采用 UTF-8，避免 cl 按系统代码页读取。
        target_compile_options(SDL2-static PRIVATE /utf-8)
    endif()
    if(DEFINED _saved_pkg_config_libdir)
        set(ENV{PKG_CONFIG_LIBDIR} "${_saved_pkg_config_libdir}")
    endif()

    set(SIMULATOR_THREAD_LIBS)
    if(NOT JY_APP_TARGET_PLATFORM STREQUAL "msvc" AND NOT SIMULATOR_USE_WIN32_THREAD_SHIM)
        find_package(Threads REQUIRED)
        set(SIMULATOR_THREAD_LIBS Threads::Threads)
    endif()

    target_link_libraries(jy_lvgl PRIVATE SDL2::SDL2-static)
    target_link_libraries(${target_name} PRIVATE
        SDL2::SDL2-static
        ${SIMULATOR_THREAD_LIBS}
        ${PLATFORM_LINK_LIBS}
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /FImsvc_compat.h)
        target_compile_options(jy_lvgl PRIVATE /FImsvc_compat.h)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE -include time.h)
        target_compile_options(jy_lvgl PRIVATE -include time.h)
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/jyt_d"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/romfs"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/jyt_c"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/jyt_d"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_sim_font_dir}"
        ${_sim_font_prepare_cmds}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_ROOT}/lfsc/system"
                "${CMAKE_BINARY_DIR}/jyt_c/system"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_ROOT}/lfsd"
                "${CMAKE_BINARY_DIR}/jyt_c/lfsd"
        COMMAND "${Python3_EXECUTABLE}" "${_font_md5_script}"
                ${_sim_font_md5_args}
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_ROOT}/scripts/StringPool.py"
                --csv "${PROJECT_ROOT}/StringPool.csv"
                --json-out "${CMAKE_BINARY_DIR}/jyt_c/system/i18n"
    )

    set(SIMULATOR_INSTALL_RUNTIME_FILES)

    if(JY_APP_TARGET_PLATFORM STREQUAL "mingw")

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
    endif()

    install(TARGETS ${target_name}
        RUNTIME DESTINATION "."
    )
    install(DIRECTORY "${CMAKE_BINARY_DIR}/jyt_d/"
        DESTINATION "jyt_d"
    )
    install(DIRECTORY "${CMAKE_BINARY_DIR}/jyt_c/"
        DESTINATION "jyt_c"
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
