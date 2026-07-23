/**
 * @file guide_runtime.c
 * @brief 开机引导教学运行态共享实现。
 */
#include "guide_runtime.h"

static guide_runtime_state_t s_guide_runtime_state = GUIDE_RUNTIME_STATE_IDLE; ///< 当前开机引导教学运行态。

void guide_runtime_reset(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_IDLE;
}

void guide_runtime_enter_home_step1(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP1_CLICK;
}

void guide_runtime_enter_home_step2_success(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP2_SUCCESS;
}

void guide_runtime_enter_home_step2_wait_back(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP2_WAIT_BACK;
}

void guide_runtime_enter_home_step3_wait_back(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP3_WAIT_BACK;
}

void guide_runtime_enter_home_step3_wait_forward(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP3_WAIT_FORWARD;
}

void guide_runtime_enter_home_step3_success(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP3_SUCCESS;
}

void guide_runtime_enter_home_step4_wait_sleep(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP4_WAIT_SLEEP;
}

void guide_runtime_enter_home_step4_sleeping(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP4_SLEEPING;
}

void guide_runtime_enter_home_step4_success(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP4_SUCCESS;
}

void guide_runtime_enter_home_step5_wait_assistant(void) {
    s_guide_runtime_state = GUIDE_RUNTIME_STATE_HOME_STEP5_WAIT_ASSISTANT;
}

guide_runtime_state_t guide_runtime_get_state(void) {
    return s_guide_runtime_state;
}

bool guide_runtime_is_home_step1(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP1_CLICK;
}

bool guide_runtime_is_home_step2_success(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP2_SUCCESS;
}

bool guide_runtime_is_home_step2_wait_back(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP2_WAIT_BACK;
}

bool guide_runtime_is_home_step3_wait_back(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP3_WAIT_BACK;
}

bool guide_runtime_is_home_step3_wait_forward(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP3_WAIT_FORWARD;
}

bool guide_runtime_is_home_step3_success(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP3_SUCCESS;
}

bool guide_runtime_is_home_step4_wait_sleep(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP4_WAIT_SLEEP;
}

bool guide_runtime_is_home_step4_sleeping(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP4_SLEEPING;
}

bool guide_runtime_is_home_step4_success(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP4_SUCCESS;
}

bool guide_runtime_is_home_step5_wait_assistant(void) {
    return s_guide_runtime_state == GUIDE_RUNTIME_STATE_HOME_STEP5_WAIT_ASSISTANT;
}
