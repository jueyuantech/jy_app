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
 * @brief 更新当前佩戴状态，用于在未佩戴时屏蔽触控板输入。
 * @param[in] worn `true` 表示已佩戴，`false` 表示未佩戴。
 * @return 无返回值。
 */
void system_runtime_input_set_wearing_state(bool worn);
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
 * @brief 将 IMU 抬头/低头事件直接映射为系统亮灭屏。
 * @param[in] msg IMU 方向消息。
 * @return `true` 表示事件已处理，`false` 表示处理失败。
 */
bool system_update_imu_tilt(JYT_ELF_MQ_MSG* msg);

#ifdef __cplusplus
}
#endif
