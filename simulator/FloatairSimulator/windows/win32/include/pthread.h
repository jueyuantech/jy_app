#pragma once

#if defined(_MSC_VER) || defined(FLOATAIR_USE_WIN32_PTHREAD_SHIM)

#include <errno.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef ENOTSUP
#define ENOTSUP EINVAL
#endif

typedef HANDLE pthread_t;

typedef struct {
    INIT_ONCE once;
    CRITICAL_SECTION section;
    volatile LONG initialized;
} pthread_mutex_t;

typedef CONDITION_VARIABLE pthread_cond_t;

#define PTHREAD_MUTEX_INITIALIZER { INIT_ONCE_STATIC_INIT, { 0 }, 0 }
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT

typedef struct {
    void* (*start_routine)(void*);
    void* arg;
} pthread_start_data_t;

/**
 * @brief 使用 Win32 系统时间填充 POSIX timespec 实时时钟。
 * @param[in,out] ts 输出的时间结构。
 * @return 成功返回 0。
 */
static inline int simulator_clock_gettime_realtime(struct timespec* ts)
{
    FILETIME ft;
    ULARGE_INTEGER now;
    uint64_t unix_time_100ns = 0;

    GetSystemTimeAsFileTime(&ft);
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;
    unix_time_100ns = now.QuadPart - 116444736000000000ULL;
    ts->tv_sec = (time_t)(unix_time_100ns / 10000000ULL);
    ts->tv_nsec = (long)((unix_time_100ns % 10000000ULL) * 100ULL);
    return 0;
}

/**
 * @brief 使用高精度性能计数器填充 POSIX timespec 单调时钟。
 * @param[in,out] ts 输出的时间结构。
 * @return 成功返回 0；性能计数器不可用时回退到实时时钟。
 */
static inline int simulator_clock_gettime_monotonic(struct timespec* ts)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    uint64_t seconds = 0;
    uint64_t ticks = 0;

    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
            !QueryPerformanceCounter(&counter)) {
        return simulator_clock_gettime_realtime(ts);
    }

    seconds = (uint64_t)(counter.QuadPart / frequency.QuadPart);
    ticks = (uint64_t)(counter.QuadPart % frequency.QuadPart);
    ts->tv_sec = (time_t)seconds;
    ts->tv_nsec = (long)((ticks * 1000000000ULL) / (uint64_t)frequency.QuadPart);
    return 0;
}

/**
 * @brief 在 Win32 pthread shim 下提供 POSIX clock_gettime 兼容实现。
 * @param[in] clock_id 时钟类型，支持 CLOCK_MONOTONIC 和 CLOCK_REALTIME。
 * @param[in,out] ts 输出的时间结构。
 * @return 成功返回 0，失败返回 -1 并设置 errno。
 */
static inline int simulator_clock_gettime(int clock_id, struct timespec* ts)
{
    if (!ts) {
        errno = EINVAL;
        return -1;
    }

    if (clock_id == CLOCK_MONOTONIC) {
        return simulator_clock_gettime_monotonic(ts);
    }
    if (clock_id == CLOCK_REALTIME) {
        return simulator_clock_gettime_realtime(ts);
    }

    errno = EINVAL;
    return -1;
}

#ifdef clock_gettime
#undef clock_gettime
#endif
/* 将本 shim 内的 clock_gettime 调用统一收敛到 Win32 兼容实现。 */
#define clock_gettime simulator_clock_gettime

static inline BOOL CALLBACK pthread_mutex_init_once(PINIT_ONCE once, PVOID parameter, PVOID* context)
{
    (void)once;
    (void)context;
    InitializeCriticalSection(&((pthread_mutex_t*)parameter)->section);
    InterlockedExchange(&((pthread_mutex_t*)parameter)->initialized, 1);
    return TRUE;
}

static inline void pthread_mutex_ensure_initialized(pthread_mutex_t* mutex)
{
    InitOnceExecuteOnce(&mutex->once, pthread_mutex_init_once, mutex, NULL);
}

static inline int pthread_mutex_init(pthread_mutex_t* mutex, const void* attr)
{
    (void)attr;
    if (!mutex) {
        return EINVAL;
    }

    *mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_ensure_initialized(mutex);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t* mutex)
{
    if (!mutex) {
        return EINVAL;
    }

    if (mutex->initialized) {
        DeleteCriticalSection(&mutex->section);
        InterlockedExchange(&mutex->initialized, 0);
    }
    return 0;
}

static inline unsigned __stdcall pthread_start_thunk(void* arg)
{
    pthread_start_data_t* data = (pthread_start_data_t*)arg;
    void* (*start_routine)(void*) = data->start_routine;
    void* start_arg = data->arg;
    free(data);
    (void)start_routine(start_arg);
    return 0;
}

static inline int pthread_create(pthread_t* thread,
                                 const void* attr,
                                 void* (*start_routine)(void*),
                                 void* arg)
{
    pthread_start_data_t* data = NULL;
    uintptr_t handle = 0;

    (void)attr;
    if (!thread || !start_routine) {
        return EINVAL;
    }

    data = (pthread_start_data_t*)malloc(sizeof(*data));
    if (!data) {
        return ENOMEM;
    }

    data->start_routine = start_routine;
    data->arg = arg;
    handle = _beginthreadex(NULL, 0, pthread_start_thunk, data, 0, NULL);
    if (handle == 0) {
        free(data);
        return errno ? errno : EAGAIN;
    }

    *thread = (HANDLE)handle;
    return 0;
}

static inline int pthread_join(pthread_t thread, void** retval)
{
    (void)retval;
    if (!thread) {
        return EINVAL;
    }
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

static inline void pthread_exit(void* retval)
{
    (void)retval;
    _endthreadex(0);
}

static inline int pthread_detach(pthread_t thread)
{
    if (!thread) {
        return EINVAL;
    }
    CloseHandle(thread);
    return 0;
}

static inline int pthread_cancel(pthread_t thread)
{
    (void)thread;
    return ENOTSUP;
}

static inline int pthread_mutex_lock(pthread_mutex_t* mutex)
{
    if (!mutex) {
        return EINVAL;
    }
    pthread_mutex_ensure_initialized(mutex);
    EnterCriticalSection(&mutex->section);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t* mutex)
{
    if (!mutex) {
        return EINVAL;
    }
    LeaveCriticalSection(&mutex->section);
    return 0;
}

static inline int pthread_cond_init(pthread_cond_t* cond, const void* attr)
{
    (void)attr;
    if (!cond) {
        return EINVAL;
    }

    InitializeConditionVariable(cond);
    return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t* cond)
{
    if (!cond) {
        return EINVAL;
    }

    return 0;
}


static inline int pthread_cond_signal(pthread_cond_t* cond)
{
    if (!cond) {
        return EINVAL;
    }
    WakeConditionVariable(cond);
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t* cond)
{
    if (!cond) {
        return EINVAL;
    }
    WakeAllConditionVariable(cond);
    return 0;
}

static inline int pthread_cond_timedwait(pthread_cond_t* cond,
                                         pthread_mutex_t* mutex,
                                         const struct timespec* abstime)
{
    struct timespec now;
    int64_t timeout_ms = 0;

    if (!cond || !mutex || !abstime) {
        return EINVAL;
    }

    clock_gettime(CLOCK_REALTIME, &now);
    timeout_ms = ((int64_t)abstime->tv_sec - (int64_t)now.tv_sec) * 1000 +
                 ((int64_t)abstime->tv_nsec - (int64_t)now.tv_nsec) / 1000000;
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }

    if (SleepConditionVariableCS(cond, &mutex->section, (DWORD)timeout_ms)) {
        return 0;
    }
    return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
}

#else
#include_next <pthread.h>
#endif
