/**
 * @file system_runtime_input.h
 * @brief 系统运行时输入事件接口声明
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#pragma once

#include "system/system_runtime_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 处理系统触摸事件并分发到当前页面。
 * @param[in] event 系统触摸事件值。
 * @return `true` 表示事件已处理，`false` 表示处理失败。
 */
bool system_touch_event(uint8_t event);
/**
 * @brief 按可信状态规则更新当前佩戴状态。
 * @param[in] worn `true` 表示已佩戴，`false` 表示未佩戴。
 * @return `true` 表示本次状态可信并已生效，`false` 表示未确认前的摘下事件已忽略。
 */
bool system_runtime_input_update_wearing_state(bool worn);

/**
 * @brief 重置佩戴可信状态，恢复为默认已佩戴但未确认。
 * @return 无返回值。
 */
void system_runtime_input_reset_wearing_state(void);

/**
 * @brief 获取系统亮灭屏状态变化事件 ID。
 * @return 返回 LVGL 自定义事件 ID。
 */
uint32_t system_runtime_input_get_sys_state_event(void);
/**
 * @brief 向当前 App 页面通知系统亮灭屏状态变化。
 * @param[in] state 系统屏幕状态，`0` 表示灭屏，`1` 表示亮屏；其他值无效。
 * @return `true` 表示通知已处理或当前页面无需通知，`false` 表示当前没有可通知的页面。
 */
bool system_runtime_input_notify_sys_state(uint8_t state);
/**
 * @brief 处理 force 触控事件并分发到当前页面。
 * @param[in] event force 触控事件值。
 * @return `true` 表示事件已处理，`false` 表示处理失败。
 */
bool system_touch_event_convert(uint8_t event);
/**
 * @brief 处理 IMU 点击事件。
 * @param[in] event IMU 事件值。
 * @return `true` 表示事件已处理，`false` 表示处理失败。
 */
bool system_imu_event_convert_to_touch(uint8_t event);
/**
 * @brief 按静态检测配置处理 IMU 运动/静止事件并控制亮灭屏。
 * @param[in] event `SET_IMU_ANY_MOTION` 或 `SET_IMU_NO_MOTION`。
 * @return `true` 表示事件已处理，`false` 表示事件类型无效。
 */
bool system_update_imu_motion(uint16_t event);
/**
 * @brief 将 IMU 抬头/低头事件直接映射为系统亮灭屏。
 * @param[in] msg IMU 方向消息。
 * @return `true` 表示事件已处理，`false` 表示处理失败。
 */
bool system_update_imu_tilt(JYT_ELF_MQ_MSG* msg);

#ifdef __cplusplus
}
#endif
