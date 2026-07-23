/**
 * @file guide_runtime.h
 * @brief 开机引导教学运行态共享接口。
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 开机引导教学运行态。
 */
typedef enum {
    GUIDE_RUNTIME_STATE_IDLE = 0,                    ///< 未处于教学流程。
    GUIDE_RUNTIME_STATE_HOME_STEP1_CLICK,            ///< 教学 1/5：Home 等待单击进入翻译。
    GUIDE_RUNTIME_STATE_HOME_STEP2_WAIT_BACK,        ///< 教学 2/5：Home 展示翻译引导并等待双击返回。
    GUIDE_RUNTIME_STATE_HOME_STEP2_SUCCESS,          ///< 教学 2/5：Home 展示返回成功。
    GUIDE_RUNTIME_STATE_HOME_STEP3_WAIT_BACK,        ///< 教学 3/5：Home 等待向后滑动。
    GUIDE_RUNTIME_STATE_HOME_STEP3_WAIT_FORWARD,     ///< 教学 3/5：Home 等待向前滑动。
    GUIDE_RUNTIME_STATE_HOME_STEP3_SUCCESS,          ///< 教学 3/5：Home 展示切回成功。
    GUIDE_RUNTIME_STATE_HOME_STEP4_WAIT_SLEEP,       ///< 教学 4/5：Home 等待双击息屏。
    GUIDE_RUNTIME_STATE_HOME_STEP4_SLEEPING,         ///< 教学 4/5：Home 已进入息屏。
    GUIDE_RUNTIME_STATE_HOME_STEP4_SUCCESS,          ///< 教学 4/5：Home 展示息屏亮屏成功。
    GUIDE_RUNTIME_STATE_HOME_STEP5_WAIT_ASSISTANT,   ///< 教学 5/5：Home 等待语音唤醒完成。
} guide_runtime_state_t;

/**
 * @brief 清空开机引导教学运行态。
 * @return 无返回值。
 */
void guide_runtime_reset(void);

/**
 * @brief 进入 Home 教学 1/5 单击状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step1(void);

/**
 * @brief 进入 Home 教学 2/5 成功状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step2_success(void);

/**
 * @brief 进入 Home 教学 2/5 等待双击返回状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step2_wait_back(void);

/**
 * @brief 进入 Home 教学 3/5 等待向后滑动状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step3_wait_back(void);

/**
 * @brief 进入 Home 教学 3/5 等待向前滑动状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step3_wait_forward(void);

/**
 * @brief 进入 Home 教学 3/5 成功状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step3_success(void);

/**
 * @brief 进入 Home 教学 4/5 等待双击息屏状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step4_wait_sleep(void);

/**
 * @brief 进入 Home 教学 4/5 已息屏状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step4_sleeping(void);

/**
 * @brief 进入 Home 教学 4/5 成功状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step4_success(void);

/**
 * @brief 进入 Home 教学 5/5 等待语音唤醒状态。
 * @return 无返回值。
 */
void guide_runtime_enter_home_step5_wait_assistant(void);

/**
 * @brief 获取当前开机引导教学运行态。
 * @return 当前运行态。
 */
guide_runtime_state_t guide_runtime_get_state(void);

/**
 * @brief 判断当前是否处于 Home 教学 1/5 单击状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step1(void);

/**
 * @brief 判断当前是否处于 Home 教学 2/5 成功状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step2_success(void);

/**
 * @brief 判断当前是否处于 Home 教学 2/5 等待双击返回状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step2_wait_back(void);

/**
 * @brief 判断当前是否处于 Home 教学 3/5 等待向后滑动状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step3_wait_back(void);

/**
 * @brief 判断当前是否处于 Home 教学 3/5 等待向前滑动状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step3_wait_forward(void);

/**
 * @brief 判断当前是否处于 Home 教学 3/5 成功状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step3_success(void);

/**
 * @brief 判断当前是否处于 Home 教学 4/5 等待双击息屏状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step4_wait_sleep(void);

/**
 * @brief 判断当前是否处于 Home 教学 4/5 已息屏状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step4_sleeping(void);

/**
 * @brief 判断当前是否处于 Home 教学 4/5 成功状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step4_success(void);

/**
 * @brief 判断当前是否处于 Home 教学 5/5 等待语音唤醒状态。
 * @return `true` 表示处于该状态，`false` 表示不是。
 */
bool guide_runtime_is_home_step5_wait_assistant(void);

#ifdef __cplusplus
}
#endif
