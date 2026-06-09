#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SYSTEM_LVGL_TICK_PERIOD (200) // 200ms
#define SYSTEM_LVGL_SECOND_PERIOD (1000) // 1 second 1000ms
#define SYSTEM_LVGL_MINUTE_PERIOD (1000 * 60) // 1 minute

#define SYSTEM_TIMER_ID_SLEEP 0 // sleep timer
#define SYSTEM_TIMER_ID_LVGL_PERIOD 1 // lvgl period timer

#define SYSTEM_TIMER_AUTO_DESTROY1 11 // auto destroy timer
#define SYSTEM_TIMER_AUTO_DESTROY2 12 // auto destroy timer 2
#define SYSTEM_TIMER_AUTO_DESTROY3 13 // auto destroy timer 3
#define SYSTEM_TIMER_AUTO_DESTROY4 14 // auto destroy timer 4
#define SYSTEM_TIMER_AUTO_DESTROY5 15 // auto destroy timer 5

typedef void (*system_timer_autodestroy_cb_t)(void* user_data);

void system_timer_init(void);
void system_timer_deinit(void);

bool system_timer_autodestroy_start(uint32_t timeout_ms,
                                   system_timer_autodestroy_cb_t cb,
                                   void* user_data,
                                   uint32_t* out_timer_id);
bool system_timer_autodestroy_cancel(uint32_t timer_id);
bool system_timer_handle_trigger(uint32_t timer_id);

bool system_timer_sleep_init(uint32_t timeout_ms);
void system_timer_sleep_reset(void);
void system_timer_sleep_deinit(void);

bool system_timer_lvgl_period_start(void);
void system_timer_lvgl_period_stop(void);
