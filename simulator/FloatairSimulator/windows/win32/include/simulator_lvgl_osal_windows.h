#pragma once

#if LV_USE_OS == LV_OS_CUSTOM

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <stdbool.h>

typedef HANDLE lv_thread_t;

typedef CRITICAL_SECTION lv_mutex_t;

typedef struct {
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    bool v;
} lv_thread_sync_t;

#endif
