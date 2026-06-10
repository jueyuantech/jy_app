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

static inline void floatair_log_internal(int level, const char* tag, const char* func, int line, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    char time_str[32];
    long time_msec = 0;
    simulator_platform_get_log_time(time_str, sizeof(time_str), &time_msec);
    
    // Level prefix
    const char* lvl_str = "DBG";
    if (level == FLOATAIR_DBG_LVL_ERR) lvl_str = "ERR";
    else if (level == FLOATAIR_DBG_LVL_WARN) lvl_str = "WRN";
    else if (level == FLOATAIR_DBG_LVL_INFO) lvl_str = "INF";
    
    fprintf(stderr, "[%s.%03ld][%s][%s] ", time_str, time_msec, lvl_str, tag ? tag : "");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, " (%s:%d)\n", func, line);
    
    va_end(args);
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
