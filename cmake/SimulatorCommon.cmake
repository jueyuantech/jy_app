include_guard(GLOBAL)

function(jy_app_prepare_simulator_common)
    set(_simulator_dir "${PROJECT_ROOT}/simulator/FloatairSimulator")

    set(TARGET_NAME "floatair_simulator" PARENT_SCOPE)
    set(SIMULATOR_DIR "${_simulator_dir}" PARENT_SCOPE)

    set(PLATFORM_INCLUDE_DIRS
        "${_simulator_dir}"
        "${_simulator_dir}/nuttx"
        "${JY_APP_OS_SDK_FLOATAIR_DIR}"
        "${PROJECT_ROOT}/bes28"
        PARENT_SCOPE
    )

    set(PLATFORM_SOURCES
        "${_simulator_dir}/main.c"
        "${_simulator_dir}/simulator_event_fifo.c"
        "${_simulator_dir}/sys_adapter.c"
        "${_simulator_dir}/floatair_lcd.c"
        "${_simulator_dir}/sim_socket.c"
        "${_simulator_dir}/floatair_fs.c"
        "${_simulator_dir}/lv_port_fs.c"
        "${_simulator_dir}/simulator_platform.c"
        PARENT_SCOPE
    )

    set(JY_APP_SIMULATOR_COMMON_COMPILE_DEFS
        _DEBUG
        _CONSOLE
        LV_CONF_SKIP
        BUILD_NATIVE=1
        LV_USE_WINDOWS=0
        LV_COLOR_DEPTH=16
        LV_FS_DEFAULT_DRIVE_LETTER='A'
        LV_USE_TINY_TTF=1
        LV_TINY_TTF_FILE_SUPPORT=1
        LV_TINY_TTF_CACHE_GLYPH_CNT=1
        LV_USE_TJPGD=1
        LV_FONT_UNSCII_8=1
        LV_FONT_MONTSERRAT_18=1
        LV_FONT_MONTSERRAT_24=1
        LV_FONT_DEFAULT=&lv_font_unscii_8
        LV_USE_FONT_PLACEHOLDER=1
        LV_TXT_ENC=LV_TXT_ENC_UTF8
        LV_TXT_BREAK_CHARS=" ,"
        LV_TXT_LINE_BREAK_LONG_LEN=0
        LV_WIDGETS_HAS_DEFAULT_VALUE=1
        LV_COLOR_MIX_ROUND_OFS=128
        LV_USE_LOG=1
        LV_LOG_LEVEL=LV_LOG_LEVEL_WARN
        LV_LOG_USE_TIMESTAMP=1
        LV_LOG_USE_FILE_LINE=1
        LV_LOG_TRACE_MEM=1
        LV_LOG_TRACE_TIMER=1
        LV_LOG_TRACE_INDEV=1
        LV_LOG_TRACE_DISP_REFR=1
        LV_LOG_TRACE_EVENT=1
        LV_LOG_TRACE_OBJ_CREATE=1
        LV_LOG_TRACE_LAYOUT=1
        LV_LOG_TRACE_ANIM=1
        LV_LOG_TRACE_CACHE=1
        LV_USE_STDLIB_MALLOC=1
        LV_USE_STDLIB_STRING=1
        LV_USE_STDLIB_SPRINTF=1
        LV_USE_SDL=1
        LV_ASSERT_HANDLER_INCLUDE="floatair_dbg.h"
        PARENT_SCOPE
    )

    configure_file(
        "${_simulator_dir}/simulator_socket.conf"
        "${CMAKE_BINARY_DIR}/simulator_socket.conf"
        COPYONLY
    )
endfunction()
