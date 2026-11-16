set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(THIRDPARTY_DIR "${PROJECT_ROOT}/thirdparty")
set(APPS_DIR "${PROJECT_ROOT}/apps")
set(SYSTEM_DIR "${PROJECT_ROOT}/system")
set(COMMON_DIR "${PROJECT_ROOT}/common")
set(LVGL_DIR "${PROJECT_ROOT}/lvgl")
set(CJSON_DIR "${THIRDPARTY_DIR}/cJSON")
set(I18N_DIR "${THIRDPARTY_DIR}/i18n")
set(MPACK_DIR "${THIRDPARTY_DIR}/mpack")
set(UI_COMPILER_DIR "${PROJECT_ROOT}/tools/ui_compiler")
set(GENERATED_UI_DIR "${CMAKE_BINARY_DIR}/generated/ui")
set(PRODUCT_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/product")
set(PRODUCT_MANIFEST "${PROJECT_ROOT}/products/${JY_APP_PRODUCT}/product.json")

if(CONFIG_LV_USE_BIDI OR CONFIG_LV_USE_HARFBUZZ_SHAPER)
    set(SHEENBIDI_DIR "${THIRDPARTY_DIR}/sheenbidi")
    set(SHEENBIDI_INCLUDE_DIRS
        "${SHEENBIDI_DIR}/Headers"
        "${SHEENBIDI_DIR}/Source"
    )
    set(SHEENBIDI_SOURCES
        "${SHEENBIDI_DIR}/Source/SheenBidi.c"
    )
endif()

if(CONFIG_LV_USE_HARFBUZZ_SHAPER AND NOT JY_APP_TARGET_PLATFORM STREQUAL "arm")
    include(HarfBuzzSources)
    set(HARFBUZZ_BRIDGE_SOURCE "${LVGL_DIR}/src/misc/lv_text_shape_hb.cc")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(NOT EXISTS "${PRODUCT_MANIFEST}")
    message(FATAL_ERROR "Product manifest not found: ${PRODUCT_MANIFEST}")
endif()

file(MAKE_DIRECTORY "${PRODUCT_GENERATED_DIR}")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_ROOT}/scripts/generate_product_apps.py"
            --manifest "${PRODUCT_MANIFEST}"
            --apps-dir "${APPS_DIR}"
            --output-dir "${PRODUCT_GENERATED_DIR}"
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    RESULT_VARIABLE _product_apps_result
)
if(NOT _product_apps_result EQUAL 0)
    message(FATAL_ERROR
        "generate_product_apps.py failed for ${JY_APP_PRODUCT} "
        "with exit code ${_product_apps_result}")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PRODUCT_MANIFEST}"
    "${PROJECT_ROOT}/scripts/generate_product_apps.py"
)
include("${PRODUCT_GENERATED_DIR}/product_app_sources.cmake")

# 从 product.json 读取字体类型配置 (font_type / bitmap_font_size / bitmap_bpp)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c
            "import json,sys;d=json.load(open(sys.argv[1]));print(d.get('font_type','freetype'));print(d.get('bitmap_font_size',32));print(d.get('bitmap_bpp',8))"
            "${PRODUCT_MANIFEST}"
    OUTPUT_VARIABLE _font_cfg_out
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _font_cfg_result
)
if(NOT _font_cfg_result EQUAL 0)
    message(FATAL_ERROR "Failed to read font_type from ${PRODUCT_MANIFEST}")
endif()
string(REPLACE "\n" ";" _font_cfg_list "${_font_cfg_out}")
list(GET _font_cfg_list 0 JY_APP_FONT_TYPE)
list(GET _font_cfg_list 1 JY_APP_BITMAP_FONT_SIZE)
list(GET _font_cfg_list 2 JY_APP_BITMAP_BPP)
message(STATUS "Font configuration: type=${JY_APP_FONT_TYPE} size=${JY_APP_BITMAP_FONT_SIZE} bpp=${JY_APP_BITMAP_BPP}")

file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${APPS_DIR}/*.c")
set(UI_JSON_SOURCES)
foreach(_app_common_module IN LISTS PRODUCT_APP_COMMON_MODULES)
    file(GLOB_RECURSE _app_common_sources CONFIGURE_DEPENDS
        "${APPS_DIR}/common/${_app_common_module}/*.c"
    )
    file(GLOB_RECURSE _app_common_ui_sources CONFIGURE_DEPENDS
        "${APPS_DIR}/common/${_app_common_module}/*.ui.json"
    )
    list(APPEND APP_SOURCES ${_app_common_sources})
    list(APPEND UI_JSON_SOURCES ${_app_common_ui_sources})
endforeach()
foreach(_app_module IN LISTS PRODUCT_APP_MODULES)
    file(GLOB_RECURSE _app_module_sources CONFIGURE_DEPENDS
        "${APPS_DIR}/${_app_module}/*.c"
    )
    file(GLOB_RECURSE _app_module_ui_sources CONFIGURE_DEPENDS
        "${APPS_DIR}/${_app_module}/*.ui.json"
    )
    list(APPEND APP_SOURCES ${_app_module_sources})
    list(APPEND UI_JSON_SOURCES ${_app_module_ui_sources})
endforeach()

file(GLOB_RECURSE SYSTEM_SOURCES CONFIGURE_DEPENDS "${SYSTEM_DIR}/*.c")
file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS "${COMMON_DIR}/*.c")
file(GLOB_RECURSE LVGL_SOURCES "${LVGL_DIR}/src/*.c")
file(GLOB_RECURSE SYSTEM_UI_JSON_SOURCES CONFIGURE_DEPENDS "${SYSTEM_DIR}/*.ui.json")
list(APPEND UI_JSON_SOURCES ${SYSTEM_UI_JSON_SOURCES})

set(RES_JSON_SOURCES "${PROJECT_ROOT}/ui.res.json")

set(GENERATED_UI_SOURCES)
set(GENERATED_UI_HEADERS)
set(GENERATED_RES_HEADERS)

foreach(_res_json IN LISTS RES_JSON_SOURCES)
    get_filename_component(_res_name_we "${_res_json}" NAME_WE)
    string(REGEX REPLACE "\\.res$" "" _res_name "${_res_name_we}")
    set(_res_header "${GENERATED_UI_DIR}/${_res_name}_res.h")
    add_custom_command(
        OUTPUT "${_res_header}"
        COMMAND "${Python3_EXECUTABLE}" "${UI_COMPILER_DIR}/rcc.py"
                "${_res_json}"
                --out-dir "${GENERATED_UI_DIR}"
        DEPENDS "${_res_json}" "${UI_COMPILER_DIR}/rcc.py"
        VERBATIM
    )
    list(APPEND GENERATED_RES_HEADERS "${_res_header}")
endforeach()

foreach(_ui_json IN LISTS UI_JSON_SOURCES)
    get_filename_component(_ui_name_we "${_ui_json}" NAME_WE)
    string(REGEX REPLACE "\\.ui$" "" _ui_name "${_ui_name_we}")
    set(_ui_header "${GENERATED_UI_DIR}/${_ui_name}_ui.h")
    set(_ui_source "${GENERATED_UI_DIR}/${_ui_name}_ui.c")
    set(_ui_deps "${_ui_json}" "${UI_COMPILER_DIR}/uic.py" ${GENERATED_RES_HEADERS})
    add_custom_command(
        OUTPUT "${_ui_header}" "${_ui_source}"
        COMMAND "${Python3_EXECUTABLE}" "${UI_COMPILER_DIR}/uic.py"
                "${_ui_json}"
                --out-dir "${GENERATED_UI_DIR}"
        DEPENDS ${_ui_deps}
        VERBATIM
    )
    list(APPEND GENERATED_UI_HEADERS "${_ui_header}")
    list(APPEND GENERATED_UI_SOURCES "${_ui_source}")
endforeach()

set(CJSON_SOURCES
    "${CJSON_DIR}/cJSON.c"
    "${CJSON_DIR}/cJSON_Utils.c"
)

set(I18N_SOURCES
    "${I18N_DIR}/i18n.c"
)

set(MPACK_SOURCES
    "${MPACK_DIR}/mpack-common.c"
    "${MPACK_DIR}/mpack-expect.c"
    "${MPACK_DIR}/mpack-node.c"
    "${MPACK_DIR}/mpack-platform.c"
    "${MPACK_DIR}/mpack-reader.c"
    "${MPACK_DIR}/mpack-writer.c"
)

set(TARGET_SOURCES
    ${CJSON_SOURCES}
    ${I18N_SOURCES}
    ${MPACK_SOURCES}
    ${APP_SOURCES}
    ${SYSTEM_SOURCES}
    ${COMMON_SOURCES}
    "${PRODUCT_GENERATED_DIR}/product_app_generated.c"
    ${GENERATED_UI_SOURCES}
    ${GENERATED_UI_HEADERS}
    ${GENERATED_RES_HEADERS}
)

set(COMMON_INCLUDE_DIRS
    "${PROJECT_ROOT}"
    "${CMAKE_BINARY_DIR}/generated"
    "${PRODUCT_GENERATED_DIR}"
    "${GENERATED_UI_DIR}"
    "${APPS_DIR}"
    "${SYSTEM_DIR}"
    "${COMMON_DIR}"
    "${LVGL_DIR}"
    "${LVGL_DIR}/src"
    "${CJSON_DIR}"
    "${I18N_DIR}"
    "${MPACK_DIR}"
    ${SHEENBIDI_INCLUDE_DIRS}
    ${HARFBUZZ_INCLUDE_DIRS}
)
