#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "../../common/app_lcd.h"
#include "system/system.h"

#define FLOATAIR_UNUSED(x) (void)(x)

 

void system_delay_ms(unsigned int ms);
void simulator_request_shutdown(void);
int simulator_shutdown_requested(void);
void simulator_shutdown_runtime(void);
void simulator_lvgl_enter_ui_critical(void);
void simulator_lvgl_leave_ui_critical(void);

/**
 * @brief 向模拟器系统消息队列投递一个系统事件。
 * @param[in] event_type 系统事件类型，定义见 `SYSTEM_EVENT_TYPE`。
 * @return 无返回值。
 */
void simulator_post_system_event(uint16_t event_type);

/**
 * @brief 向模拟器系统消息队列投递一个可带 simple_data 和 payload 的系统事件。
 * @param[in] event_type 系统事件类型，定义见 `SYSTEM_EVENT_TYPE`。
 * @param[in] simple_data 消息头中的 simple_data。
 * @param[in] payload 事件 payload 缓冲区，可为 `NULL`。
 * @param[in] payload_len payload 长度，单位字节。
 * @return 无返回值。
 */
void simulator_post_system_event_ex(uint16_t event_type,
                                    uint8_t simple_data,
                                    const void* payload,
                                    uint16_t payload_len);
void simulator_update_lcd_visual(uint8_t brightness, lcd_state_t state);
