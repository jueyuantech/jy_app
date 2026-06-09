#include "system_timer.h"

#include "app_def.h"
#include "app_lcd.h"
#include "elf_common.h"
#include "floatair_dbg.h"
#include "system.h"

#include <inttypes.h>
#include <string.h>

typedef struct {
    bool in_use;
    uint32_t timer_id;
    void* timer_handle;
    system_timer_autodestroy_cb_t cb;
    void* user_data;
} system_timer_slot_t;

static system_timer_slot_t g_auto_slots[5];
static void* g_sleep_timer = NULL;
static void* g_lvgl_period_timer = NULL;

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
            g_auto_slots[i].timer_id = timer_id;
            g_auto_slots[i].cb = cb;
            g_auto_slots[i].user_data = user_data;
            g_auto_slots[i].timer_handle = jyt_timer_create_and_start(timeout_ms, timer_id, 1);

            if (g_auto_slots[i].timer_handle == NULL) {
                g_auto_slots[i].in_use = false;
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
        floatair_lvgl_tick();
        return true;
    }
    if (timer_id == SYSTEM_TIMER_ID_SLEEP) {
        if (!system_config_get_idle_detection_enabled() ||
            system_config_get_inactivity_timeout() == 0) {
            floatair_warn("ignore stale sleep timer trigger: idle_en=%d inactivity_timeout=%u",
                          (int)system_config_get_idle_detection_enabled(),
                          (unsigned int)system_config_get_inactivity_timeout());
            system_timer_sleep_deinit();
            return true;
        }
        floatair_lcd_set_state(LCD_OFF);
        system_report_sys_state(0);
        return true;
    }

    int idx = system_timer_autodestroy_id_to_index(timer_id);
    if (idx < 0) {
        floatair_err("invalid timer_id: %" PRIu32, timer_id);
        return false;
    }

    system_timer_slot_t* slot = &g_auto_slots[idx];
    if (!slot->in_use) {
        return true;
    }

    system_timer_autodestroy_cb_t cb = slot->cb;
    void* user_data = slot->user_data;

    slot->in_use = false;
    slot->timer_id = 0;
    slot->timer_handle = NULL;
    slot->cb = NULL;
    slot->user_data = NULL;

    if (cb) {
        cb(user_data);
    }
    return true;
}

bool system_timer_sleep_init(uint32_t timeout_ms) {
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

void system_timer_sleep_reset(void) {
    if (g_sleep_timer != NULL) {
        jyt_timer_restart(g_sleep_timer);
    }
}

void system_timer_sleep_deinit(void) {
    if (g_sleep_timer != NULL) {
        jyt_timer_delete(g_sleep_timer);
        g_sleep_timer = NULL;
    }
}

bool system_timer_lvgl_period_start(void) {
    if (g_lvgl_period_timer != NULL) {
        return true;
    }
    g_lvgl_period_timer = jyt_timer_create_and_start(SYSTEM_LVGL_MINUTE_PERIOD, SYSTEM_TIMER_ID_LVGL_PERIOD, 0);
    return g_lvgl_period_timer != NULL;
}

void system_timer_lvgl_period_stop(void) {
    if (g_lvgl_period_timer != NULL) {
        jyt_timer_delete(g_lvgl_period_timer);
        g_lvgl_period_timer = NULL;
    }
}
