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

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "${APPS_DIR}/*.c")
file(GLOB_RECURSE SYSTEM_SOURCES CONFIGURE_DEPENDS "${SYSTEM_DIR}/*.c")
file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS "${COMMON_DIR}/*.c")
file(GLOB_RECURSE LVGL_SOURCES "${LVGL_DIR}/src/*.c")
file(GLOB_RECURSE UI_JSON_SOURCES CONFIGURE_DEPENDS
    "${APPS_DIR}/*.ui.json"
    "${SYSTEM_DIR}/*.ui.json"
)
set(RES_JSON_SOURCES "${PROJECT_ROOT}/ui.res.json")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

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
    ${LVGL_SOURCES}
    ${CJSON_SOURCES}
    ${I18N_SOURCES}
    ${MPACK_SOURCES}
    ${APP_SOURCES}
    ${SYSTEM_SOURCES}
    ${COMMON_SOURCES}
    ${GENERATED_UI_SOURCES}
    ${GENERATED_UI_HEADERS}
    ${GENERATED_RES_HEADERS}
)

set(COMMON_INCLUDE_DIRS
    "${PROJECT_ROOT}"
    "${CMAKE_BINARY_DIR}/generated"
    "${GENERATED_UI_DIR}"
    "${APPS_DIR}"
    "${SYSTEM_DIR}"
    "${COMMON_DIR}"
    "${LVGL_DIR}"
    "${LVGL_DIR}/src"
    "${CJSON_DIR}"
    "${I18N_DIR}"
    "${MPACK_DIR}"
)
