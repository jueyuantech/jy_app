/**
 * @file simulator_event_fifo.h
 * @brief Linux 模拟器系统事件 FIFO 输入接口
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动模拟器系统事件 FIFO 监听线程。
 * @return `true` 表示启动成功，`false` 表示启动失败。
 */
bool simulator_event_fifo_start(void);

/**
 * @brief 停止模拟器系统事件 FIFO 监听线程并释放资源。
 * @return 无返回值。
 */
void simulator_event_fifo_stop(void);

#ifdef __cplusplus
}
#endif
