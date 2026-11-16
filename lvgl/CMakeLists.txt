# LVGL 静态库及文本整形依赖；各平台使用当前工具链和统一的配置宏。
file(GLOB_RECURSE LVGL_SOURCES "${LVGL_DIR}/src/*.c")

if(JY_APP_TARGET_PLATFORM STREQUAL "msvc")
    if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(_lvgl_warning_suppression_flags "/wd4018 /wd4244 /wd4267")
    else()
        set(_lvgl_warning_suppression_flags "-Wno-sign-compare -Wno-conversion")
    endif()
    set_source_files_properties(${LVGL_SOURCES} PROPERTIES
        COMPILE_FLAGS "${_lvgl_warning_suppression_flags}")
    unset(_lvgl_warning_suppression_flags)
endif()

if(SHEENBIDI_SOURCES)
    set_source_files_properties(${SHEENBIDI_SOURCES} PROPERTIES
        COMPILE_DEFINITIONS "SB_CONFIG_UNITY;SB_CONFIG_DISABLE_SCRATCH_MEMORY;SB_CONFIG_LVGL_MINIMAL"
    )
endif()

if(NOT JY_APP_TARGET_PLATFORM STREQUAL "arm")
    set(LV_TINY_TTF_SOURCE "${LVGL_DIR}/src/libs/tiny_ttf/lv_tiny_ttf.c")
    if(EXISTS "${LV_TINY_TTF_SOURCE}" AND
            (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang"))
        set_source_files_properties("${LV_TINY_TTF_SOURCE}" PROPERTIES COMPILE_FLAGS "-Wno-format")
    endif()

    set(LV_FS_CBFS_SOURCE "${LVGL_DIR}/src/libs/fsdrv/lv_fs_cbfs.c")
    if(EXISTS "${LV_FS_CBFS_SOURCE}" AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        set_source_files_properties("${LV_FS_CBFS_SOURCE}" PROPERTIES COMPILE_FLAGS "-Wno-unused-function")
    endif()

endif()

add_library(jy_lvgl STATIC
    ${LVGL_SOURCES}
    ${SHEENBIDI_SOURCES}
    ${HARFBUZZ_BRIDGE_SOURCE}
)
set_target_properties(jy_lvgl PROPERTIES
    OUTPUT_NAME lvgl
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(jy_lvgl
    PUBLIC "${LVGL_DIR}" "${LVGL_DIR}/src"
    PRIVATE ${PLATFORM_INCLUDE_DIRS} ${COMMON_INCLUDE_DIRS}
)
target_compile_definitions(jy_lvgl PUBLIC
    ${TARGET_CONFIG_COMPILE_DEFS}
    ${PLATFORM_COMPILE_DEFS}
)
target_compile_options(jy_lvgl PRIVATE ${PLATFORM_COMPILE_OPTIONS})

if(HARFBUZZ_SOURCES)
    add_library(jy_harfbuzz STATIC ${HARFBUZZ_SOURCES})
    target_include_directories(jy_harfbuzz PRIVATE
        ${HARFBUZZ_INCLUDE_DIRS}
    )
    target_compile_definitions(jy_harfbuzz PRIVATE
        ${HARFBUZZ_COMPILE_DEFS}
    )
    target_compile_options(jy_harfbuzz PRIVATE
        ${PLATFORM_COMPILE_OPTIONS}
    )
    if(MSVC)
        # 与 HarfBuzz 14.2.1 上游保持一致，仅作用于第三方库。
        target_compile_options(jy_harfbuzz PRIVATE /wd4244 /wd4267)
    endif()
    if(JY_APP_TARGET_PLATFORM STREQUAL "msvc")
        # Windows CRT 对可移植 C 接口的弃用提示仅在第三方 HarfBuzz 目标中关闭。
        target_compile_definitions(jy_harfbuzz PRIVATE
            _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    endif()
    target_link_libraries(jy_lvgl PRIVATE jy_harfbuzz)
endif()
