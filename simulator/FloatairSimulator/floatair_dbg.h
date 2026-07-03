/**
 * @file floatair_dbg.h
 * @brief Floatair 模拟器日志与断言调试接口。
 */
#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include "simulator_platform.h"

/* ---- Logging levels ---- */
#define FLOATAIR_DBG_LVL_ERR 0
#define FLOATAIR_DBG_LVL_WARN 1
#define FLOATAIR_DBG_LVL_INFO 2
#define FLOATAIR_DBG_LVL_DEBUG 3

#define FLOATAIR_DBG_LEVEL FLOATAIR_DBG_LVL_DEBUG
#define ENABLE_FLOATAIR_DUMP8
/* 模拟器默认日志文件名，落在可执行文件同目录。 */
#define FLOATAIR_SIMULATOR_LOG_FILE "floatair_simulator.log"
/* 模拟器日志路径缓冲区长度，兼容 Windows 宽字符文件打开路径。 */
#define FLOATAIR_SIMULATOR_LOG_PATH_MAX 1024

/**
 * @brief 打开模拟器追加日志文件。
 * @return 成功返回文件句柄，失败返回 NULL。
 */
static inline FILE* floatair_log_open_file(void) {
    char exe_dir[FLOATAIR_SIMULATOR_LOG_PATH_MAX] = {0};
    char log_path[FLOATAIR_SIMULATOR_LOG_PATH_MAX] = {0};

    simulator_platform_get_executable_dir(exe_dir, sizeof(exe_dir));
    if (exe_dir[0] != '\0') {
        snprintf(log_path, sizeof(log_path), "%s/%s", exe_dir, FLOATAIR_SIMULATOR_LOG_FILE);
    } else {
        snprintf(log_path, sizeof(log_path), "%s", FLOATAIR_SIMULATOR_LOG_FILE);
    }

    for (size_t i = 0; log_path[i] != '\0'; ++i) {
        if (log_path[i] == '\\') {
            log_path[i] = '/';
        }
    }

    return simulator_platform_file_open(log_path, "ab");
}

/**
 * @brief 输出一条模拟器日志到控制台和日志文件。
 * @param[in] level 日志等级。
 * @param[in] tag 日志标签。
 * @param[in] func 调用函数名。
 * @param[in] line 调用行号。
 * @param[in] fmt 格式化字符串。
 * @return 无返回值。
 */
static inline void floatair_log_internal(int level, const char* tag, const char* func, int line, const char* fmt, ...) {
    va_list args;
    char msg[1024];
    char line_buf[1280];
    char time_str[32];
    long time_msec = 0;
    const char* lvl_str = "DBG";
    FILE* log_fp = NULL;

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    simulator_platform_get_log_time(time_str, sizeof(time_str), &time_msec);
    if (level == FLOATAIR_DBG_LVL_ERR) lvl_str = "ERR";
    else if (level == FLOATAIR_DBG_LVL_WARN) lvl_str = "WRN";
    else if (level == FLOATAIR_DBG_LVL_INFO) lvl_str = "INF";

    snprintf(line_buf,
             sizeof(line_buf),
             "[%s.%03ld][%s][%s] %s (%s:%d)\n",
             time_str,
             time_msec,
             lvl_str,
             tag ? tag : "",
             msg,
             func,
             line);

    fputs(line_buf, stderr);
    fflush(stderr);

    log_fp = floatair_log_open_file();
    if (log_fp != NULL) {
        fputs(line_buf, log_fp);
        fclose(log_fp);
    }
}

#define floatair_err(...)   floatair_log_internal(FLOATAIR_DBG_LVL_ERR,   "ERR", __func__, __LINE__, __VA_ARGS__)
#define floatair_warn(...)  floatair_log_internal(FLOATAIR_DBG_LVL_WARN,  "WRN", __func__, __LINE__, __VA_ARGS__)
#define floatair_info(...)  floatair_log_internal(FLOATAIR_DBG_LVL_INFO,  "INF", __func__, __LINE__, __VA_ARGS__)
#define floatair_debug(...) floatair_log_internal(FLOATAIR_DBG_LVL_DEBUG, "DBG", __func__, __LINE__, __VA_ARGS__)
#define floatair_dbg(...)   floatair_log_internal(FLOATAIR_DBG_LVL_DEBUG, "DBG", __func__, __LINE__, __VA_ARGS__)

#define TRACE(...) floatair_info(__VA_ARGS__)

#define ASSERT(x) assert(x)

#ifdef LV_ASSERT_HANDLER
#undef LV_ASSERT_HANDLER
#endif
#define LV_ASSERT_HANDLER assert(0);

#define floatair_assert(cond, ...) do { \
    if (!(cond)) { \
        floatair_err(__VA_ARGS__); \
        assert(0); \
    } \
} while(0)

#define floatair_simple(...) do { \
    printf(__VA_ARGS__); \
    printf("\n"); \
} while(0)

#define floatair_dump8(data, len) do { \
    const uint8_t* p = (const uint8_t*)(data); \
    for(size_t i=0; i<(len); i++) printf("%02X ", p[i]); \
    printf("\n"); \
} while(0)
