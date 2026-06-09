#include "osal/lv_os.h"

#if LV_USE_OS == LV_OS_CUSTOM

#include <errno.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    void (*callback)(void *);
    void * user_data;
} simulator_lvgl_thread_init_data_t;

static unsigned __stdcall simulator_lvgl_thread_start(void * parameter);

lv_result_t lv_thread_init(lv_thread_t * thread,
                           lv_thread_prio_t prio,
                           void (*callback)(void *),
                           size_t stack_size,
                           void * user_data)
{
    static const int prio_map[] = {
        THREAD_PRIORITY_LOWEST,
        THREAD_PRIORITY_BELOW_NORMAL,
        THREAD_PRIORITY_NORMAL,
        THREAD_PRIORITY_ABOVE_NORMAL,
        THREAD_PRIORITY_HIGHEST,
    };

    if(!thread || !callback) {
        return LV_RESULT_INVALID;
    }

    simulator_lvgl_thread_init_data_t * init_data =
        malloc(sizeof(simulator_lvgl_thread_init_data_t));
    if(!init_data) {
        return LV_RESULT_INVALID;
    }

    init_data->callback = callback;
    init_data->user_data = user_data;

    *thread = (HANDLE)_beginthreadex(NULL,
                                     (unsigned)stack_size,
                                     simulator_lvgl_thread_start,
                                     init_data,
                                     0,
                                     NULL);
    if(!*thread) {
        free(init_data);
        return LV_RESULT_INVALID;
    }

    SetThreadPriority(*thread, prio_map[prio]);
    return LV_RESULT_OK;
}

lv_result_t lv_thread_delete(lv_thread_t * thread)
{
    lv_result_t result = LV_RESULT_OK;

    if(!thread || !*thread) {
        return LV_RESULT_INVALID;
    }

    if(!TerminateThread(*thread, 0)) {
        result = LV_RESULT_INVALID;
    }

    if(!CloseHandle(*thread)) {
        result = LV_RESULT_INVALID;
    }
    *thread = NULL;

    return result;
}

lv_result_t lv_mutex_init(lv_mutex_t * mutex)
{
    InitializeCriticalSection(mutex);
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_lock(lv_mutex_t * mutex)
{
    EnterCriticalSection(mutex);
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_lock_isr(lv_mutex_t * mutex)
{
    EnterCriticalSection(mutex);
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_unlock(lv_mutex_t * mutex)
{
    LeaveCriticalSection(mutex);
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_delete(lv_mutex_t * mutex)
{
    DeleteCriticalSection(mutex);
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_init(lv_thread_sync_t * sync)
{
    if(!sync) {
        return LV_RESULT_INVALID;
    }

    InitializeCriticalSection(&sync->cs);
    InitializeConditionVariable(&sync->cv);
    sync->v = false;

    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_wait(lv_thread_sync_t * sync)
{
    if(!sync) {
        return LV_RESULT_INVALID;
    }

    EnterCriticalSection(&sync->cs);
    while(!sync->v) {
        SleepConditionVariableCS(&sync->cv, &sync->cs, INFINITE);
    }
    sync->v = false;
    LeaveCriticalSection(&sync->cs);

    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_signal(lv_thread_sync_t * sync)
{
    if(!sync) {
        return LV_RESULT_INVALID;
    }

    EnterCriticalSection(&sync->cs);
    sync->v = true;
    WakeConditionVariable(&sync->cv);
    LeaveCriticalSection(&sync->cs);

    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_delete(lv_thread_sync_t * sync)
{
    if(!sync) {
        return LV_RESULT_INVALID;
    }

    DeleteCriticalSection(&sync->cs);
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_signal_isr(lv_thread_sync_t * sync)
{
    (void)sync;
    return LV_RESULT_INVALID;
}

static unsigned __stdcall simulator_lvgl_thread_start(void * parameter)
{
    simulator_lvgl_thread_init_data_t * init_data = parameter;

    if(init_data) {
        init_data->callback(init_data->user_data);
        free(init_data);
    }

    return 0;
}

#endif
