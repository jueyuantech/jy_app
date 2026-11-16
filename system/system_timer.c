#include "system_timer.h"

#include "app_def.h"
#include "app_lcd.h"
#include "common/app_framework/app_router.h"
#include "common/guide_runtime.h"
#include "elf_common.h"
#include "floatair_dbg.h"
#include "system.h"

#include <inttypes.h>
#include <string.h>
#include <time.h>

/**
 * @brief 自动销毁定时器槽位及其灭屏延迟回调状态。
 */
typedef struct {
    bool in_use;                          ///< 当前槽位是否已分配。
    bool callback_pending;                ///< 定时器到期后是否因灭屏而等待亮屏回调。
    uint32_t timer_id;                    ///< 对外暴露的自动销毁定时器 ID。
    void* timer_handle;                   ///< 底层定时器句柄；消费首次到期事件时释放并置空。
    system_timer_autodestroy_cb_t cb;     ///< 亮屏状态下执行的回调。
    void* user_data;                      ///< 回调用户数据。
} system_timer_slot_t;

static system_timer_slot_t g_auto_slots[5];
static void* g_sleep_timer = NULL;
static void* g_os_sleep_delay_timer = NULL;
static uint64_t g_os_sleep_delay_deadline_ms = 0; ///< 最近一次息屏对应的允许休眠截止时刻。
static void* g_lvgl_period_timer = NULL;
static void* g_lvgl_realign_timer = NULL;

/**
 * @brief 获取单调递增的毫秒时间。
 * @return 返回单调时钟毫秒数，读取失败时返回 0。
 */
static uint64_t system_timer_monotonic_ms(void) {
    struct timespec now = {0};

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static int system_timer_autodestroy_id_to_index(uint32_t timer_id) {
    if (timer_id == SYSTEM_TIMER_AUTO_DESTROY1) return 0;
    if (timer_id == SYSTEM_TIMER_AUTO_DESTROY2) return 1;
    if (timer_id == SYSTEM_TIMER_AUTO_DESTROY3) return 2;
    if (timer_id == SYSTEM_TIMER_AUTO_DESTROY4) return 3;
    if (timer_id == SYSTEM_TIMER_AUTO_DESTROY5) return 4;
    return -1;
}

static uint32_t system_timer_autodestroy_index_to_id(int idx) {
    if (idx == 0) return SYSTEM_TIMER_AUTO_DESTROY1;
    if (idx == 1) return SYSTEM_TIMER_AUTO_DESTROY2;
    if (idx == 2) return SYSTEM_TIMER_AUTO_DESTROY3;
    if (idx == 3) return SYSTEM_TIMER_AUTO_DESTROY4;
    if (idx == 4) return SYSTEM_TIMER_AUTO_DESTROY5;
    return 0;
}

void system_timer_init(void) {
    memset(g_auto_slots, 0, sizeof(g_auto_slots));
    system_timer_os_sleep_delay_cancel();
}

void system_timer_deinit(void) {
    for (size_t i = 0; i < sizeof(g_auto_slots) / sizeof(g_auto_slots[0]); i++) {
        if (g_auto_slots[i].in_use) {
            if (g_auto_slots[i].timer_handle) {
                jyt_timer_delete(g_auto_slots[i].timer_handle);
            }
            g_auto_slots[i].in_use = false;
            g_auto_slots[i].timer_id = 0;
            g_auto_slots[i].timer_handle = NULL;
            g_auto_slots[i].cb = NULL;
            g_auto_slots[i].user_data = NULL;
        }
    }
    system_timer_sleep_deinit();
    system_timer_os_sleep_delay_cancel();
    system_timer_lvgl_period_stop();
}

bool system_timer_autodestroy_start(uint32_t timeout_ms,
                                   system_timer_autodestroy_cb_t cb,
                                   void* user_data,
                                   uint32_t* out_timer_id) {
    if (timeout_ms == 0 || cb == NULL || out_timer_id == NULL) {
        return false;
    }

    for (size_t i = 0; i < sizeof(g_auto_slots) / sizeof(g_auto_slots[0]); i++) {
        if (!g_auto_slots[i].in_use) {
            uint32_t timer_id = system_timer_autodestroy_index_to_id((int)i);
            if (timer_id == 0) {
                return false;
            }

            g_auto_slots[i].in_use = true;
            g_auto_slots[i].callback_pending = false;
            g_auto_slots[i].timer_id = timer_id;
            g_auto_slots[i].cb = cb;
            g_auto_slots[i].user_data = user_data;
            /*
             * 使用调用方持有的定时器，并在首次到期事件中同步删除。
             * 若使用 auto_destroy=1，底层会在 UI 消费事件前释放句柄，
             * 此时取消同一定时器会造成二次释放。
             */
            g_auto_slots[i].timer_handle = jyt_timer_create_and_start(timeout_ms, timer_id, 0);

            if (g_auto_slots[i].timer_handle == NULL) {
                g_auto_slots[i].in_use = false;
                g_auto_slots[i].callback_pending = false;
                g_auto_slots[i].timer_id = 0;
                g_auto_slots[i].cb = NULL;
                g_auto_slots[i].user_data = NULL;
                return false;
            }

            *out_timer_id = timer_id;
            return true;
        }
    }

    return false;
}

bool system_timer_autodestroy_cancel(uint32_t timer_id) {
    int idx = system_timer_autodestroy_id_to_index(timer_id);
    if (idx < 0) {
        return false;
    }

    system_timer_slot_t* slot = &g_auto_slots[idx];
    if (!slot->in_use) {
        return true;
    }

    void* timer_handle = slot->timer_handle;

    slot->in_use = false;
    slot->callback_pending = false;
    slot->timer_id = 0;
    slot->timer_handle = NULL;
    slot->cb = NULL;
    slot->user_data = NULL;

    if (timer_handle) {
        jyt_timer_delete(timer_handle);
    }
    return true;
}

bool system_timer_handle_trigger(uint32_t timer_id) {
    if (timer_id == SYSTEM_TIMER_ID_LVGL_PERIOD) {
        if (g_lvgl_period_timer == NULL) {
            floatair_warn("ignore stale lvgl period timer trigger");
            return true;
        }
        floatair_lvgl_tick();
        return true;
    }
    if (timer_id == SYSTEM_TIMER_ID_LVGL_REALIGN) {
        void* timer_handle = NULL;

        if (g_lvgl_realign_timer == NULL) {
            floatair_warn("ignore stale lvgl realign timer trigger");
            return true;
        }
        timer_handle = g_lvgl_realign_timer;
        g_lvgl_realign_timer = NULL;
        jyt_timer_delete(timer_handle);
        floatair_lvgl_tick();
        (void)system_timer_lvgl_period_start();
        return true;
    }
    if (timer_id == SYSTEM_TIMER_ID_SLEEP) {
        if (g_sleep_timer == NULL) {
            floatair_info("ignore stale sleep timer trigger: id=%" PRIu32, timer_id);
            return true;
        }
        if (/*!system_config_get_idle_detection_enabled() || */
            system_config_get_inactivity_timeout() == 0) {
            floatair_warn("ignore stale sleep timer trigger: idle_en= inactivity_timeout=%u",
                          //(int)system_config_get_idle_detection_enabled(),
                          (unsigned int)system_config_get_inactivity_timeout());
            system_timer_sleep_deinit();
            return true;
        }
        if (floatair_lcd_is_off()) {
            floatair_info("ignore sleep timer trigger while lcd off");
            system_timer_sleep_deinit();
            return true;
        }
        guide_runtime_state_t guide_state = guide_runtime_get_state();
        if (app_router_is_userguide_pending() &&
            system_get_btconn_state() &&
            guide_state != GUIDE_RUNTIME_STATE_IDLE) {
            floatair_info("connected userguide active, ignore sleep timer trigger: state=%d",
                          (int)guide_state);
            (void)system_timer_sleep_reset();
            return true;
        }
        system_set_sys_state(LCD_OFF);
        (void)system_report_sys_state(LCD_OFF,
                                      SYSTEM_SYS_STATE_TRIGGER_INACTIVITY_TIMEOUT);
        return true;
    }
    if (timer_id == SYSTEM_TIMER_ID_OS_SLEEP_DELAY) {
        uint64_t now_ms = system_timer_monotonic_ms();

        if (g_os_sleep_delay_timer == NULL) {
            floatair_info("ignore stale os sleep delay timer trigger");
            return true;
        }
        if (now_ms == 0 || g_os_sleep_delay_deadline_ms == 0) {
            floatair_warn("ignore os sleep delay timer trigger without valid deadline");
            system_timer_os_sleep_delay_cancel();
            return true;
        }
        if (now_ms < g_os_sleep_delay_deadline_ms) {
            uint64_t remaining_ms = g_os_sleep_delay_deadline_ms - now_ms;

            jyt_timer_delete(g_os_sleep_delay_timer);
            g_os_sleep_delay_timer =
                jyt_timer_create_and_start((uint32_t)remaining_ms,
                                           SYSTEM_TIMER_ID_OS_SLEEP_DELAY,
                                           0);
            if (g_os_sleep_delay_timer == NULL) {
                g_os_sleep_delay_deadline_ms = 0;
                floatair_warn("restart os sleep delay timer failed");
            } else {
                floatair_info("restart early os sleep delay timer: remaining=%" PRIu64 " ms",
                              remaining_ms);
            }
            return true;
        }

        jyt_timer_delete(g_os_sleep_delay_timer);
        g_os_sleep_delay_timer = NULL;
        g_os_sleep_delay_deadline_ms = 0;
        if (floatair_lcd_is_off()) {
            floatair_info("lcd off delay elapsed, allow os sleep");
            (void)system_request_os_sleep(true);
        }
        return true;
    }

    int idx = system_timer_autodestroy_id_to_index(timer_id);
    if (idx < 0) {
        floatair_err("invalid timer_id: %" PRIu32, timer_id);
        return false;
    }

    system_timer_slot_t* slot = &g_auto_slots[idx];
    void* timer_handle = NULL;

    if (!slot->in_use) {
        return true;
    }

    timer_handle = slot->timer_handle;
    slot->timer_handle = NULL;
    if (timer_handle != NULL) {
        jyt_timer_delete(timer_handle);
    }
    if (floatair_lcd_is_off()) {
        slot->callback_pending = true;
        floatair_info("defer auto timer callback while lcd off: id=%" PRIu32, timer_id);
        return true;
    }

    system_timer_autodestroy_cb_t cb = slot->cb;
    void* user_data = slot->user_data;

    slot->in_use = false;
    slot->callback_pending = false;
    slot->timer_id = 0;
    slot->cb = NULL;
    slot->user_data = NULL;

    if (cb) {
        cb(user_data);
    }
    return true;
}

/**
 * @brief 亮屏后执行灭屏期间到期而延迟的自动销毁定时器回调。
 * @return 无返回值。
 */
void system_timer_flush_pending_after_screen_on(void) {
    if (floatair_lcd_is_off()) {
        return;
    }

    for (size_t i = 0; i < sizeof(g_auto_slots) / sizeof(g_auto_slots[0]); i++) {
        system_timer_slot_t* slot = &g_auto_slots[i];
        system_timer_autodestroy_cb_t cb = NULL;
        void* user_data = NULL;

        if (!slot->in_use || !slot->callback_pending) {
            continue;
        }

        cb = slot->cb;
        user_data = slot->user_data;
        floatair_info("flush deferred auto timer callback: id=%" PRIu32, slot->timer_id);
        slot->in_use = false;
        slot->callback_pending = false;
        slot->timer_id = 0;
        slot->timer_handle = NULL;
        slot->cb = NULL;
        slot->user_data = NULL;
        if (cb != NULL) {
            cb(user_data);
        }
    }
}

bool system_timer_sleep_start(uint32_t timeout_ms) {
    if (timeout_ms == 0) {
        return false;
    }
    if (g_sleep_timer != NULL) {
        jyt_timer_delete(g_sleep_timer);
        g_sleep_timer = NULL;
    }
    g_sleep_timer = jyt_timer_create_and_start(timeout_ms, SYSTEM_TIMER_ID_SLEEP, 0);
    return g_sleep_timer != NULL;
}

bool system_timer_sleep_reset(void) {
    if (g_sleep_timer == NULL) {
        return false;
    }

    jyt_timer_restart(g_sleep_timer);
    return true;
}

void system_timer_sleep_deinit(void) {
    if (g_sleep_timer != NULL) {
        jyt_timer_delete(g_sleep_timer);
        g_sleep_timer = NULL;
    }
}

bool system_timer_os_sleep_delay_start(void) {
    uint64_t now_ms = system_timer_monotonic_ms();

    if (now_ms == 0) {
        return false;
    }
    if (g_os_sleep_delay_timer != NULL) {
        jyt_timer_delete(g_os_sleep_delay_timer);
        g_os_sleep_delay_timer = NULL;
    }

    (void)system_request_os_sleep(false);
    g_os_sleep_delay_deadline_ms = now_ms + SYSTEM_OS_SLEEP_DELAY_MS;
    g_os_sleep_delay_timer =
        jyt_timer_create_and_start(SYSTEM_OS_SLEEP_DELAY_MS,
                                   SYSTEM_TIMER_ID_OS_SLEEP_DELAY,
                                   0);
    if (g_os_sleep_delay_timer == NULL) {
        g_os_sleep_delay_deadline_ms = 0;
    }
    return g_os_sleep_delay_timer != NULL;
}

void system_timer_os_sleep_delay_cancel(void) {
    if (g_os_sleep_delay_timer != NULL) {
        jyt_timer_delete(g_os_sleep_delay_timer);
        g_os_sleep_delay_timer = NULL;
    }
    g_os_sleep_delay_deadline_ms = 0;
    (void)system_request_os_sleep(false);
}

bool system_timer_lvgl_period_start(void) {
    if (g_lvgl_period_timer != NULL) {
        return true;
    }
    g_lvgl_period_timer = jyt_timer_create_and_start(SYSTEM_LVGL_MINUTE_PERIOD, SYSTEM_TIMER_ID_LVGL_PERIOD, 0);
    return g_lvgl_period_timer != NULL;
}

/**
 * @brief 将 LVGL 分钟定时器重排到指定时间戳的下一分钟边界。
 * @param[in] time_now 当前已同步的系统时间。
 * @return `true` 表示重排成功，`false` 表示创建定时器失败。
 */
bool system_timer_lvgl_period_realign_to_minute(time_t time_now) {
    uint32_t first_timeout_ms = SYSTEM_LVGL_MINUTE_PERIOD;
    struct tm* ptm = localtime(&time_now);

    if (ptm != NULL && ptm->tm_sec > 0 && ptm->tm_sec < 60) {
        first_timeout_ms = (uint32_t)(60 - ptm->tm_sec) * SYSTEM_LVGL_SECOND_PERIOD;
    }

    if (g_lvgl_period_timer != NULL) {
        jyt_timer_delete(g_lvgl_period_timer);
        g_lvgl_period_timer = NULL;
    }
    if (g_lvgl_realign_timer != NULL) {
        jyt_timer_delete(g_lvgl_realign_timer);
        g_lvgl_realign_timer = NULL;
    }

    /* 同自动销毁槽位一样，由首次到期事件负责释放，避免到期/取消竞态。 */
    g_lvgl_realign_timer = jyt_timer_create_and_start(first_timeout_ms, SYSTEM_TIMER_ID_LVGL_REALIGN, 0);
    if (g_lvgl_realign_timer == NULL) {
        return false;
    }

    return true;
}

void system_timer_lvgl_period_stop(void) {
    if (g_lvgl_period_timer != NULL) {
        jyt_timer_delete(g_lvgl_period_timer);
        g_lvgl_period_timer = NULL;
    }
    if (g_lvgl_realign_timer != NULL) {
        jyt_timer_delete(g_lvgl_realign_timer);
        g_lvgl_realign_timer = NULL;
    }
}
