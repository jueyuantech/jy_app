/**
 * @file guide.c
 * @brief Guide 应用生命周期、页面入口和欢迎引导实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-07-01
 * @copyright JYTek
 * @ingroup app_guide
 */
#include "guide.h"

#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_nav.h"
#include "common/app_framework/app_router.h"
#include "common/guide_runtime.h"
#include "message.h"
#include "app_def.h"
#include "system/system.h"
#include "system/system_timer.h"
#include "common/widgets/msgbox.h"
#include "common/widgets/status_bar.h"

#include <string.h>

#define GUIDE_WELCOME_SECONDS 3u
#define GUIDE_WELCOME_TICK_MS 1000u
#define GUIDE_FONT_TITLE 28u
#define GUIDE_FONT_BODY 24u

/**
 * @brief Guide 页面状态。
 */
typedef enum {
    GUIDE_PAGE_STATE_WELCOME = 0,    ///< 开机引导欢迎页。
    GUIDE_PAGE_STATE_TRANSITIONING,  ///< 正在从欢迎页进入教学步骤。
} guide_page_state_t;

static guide_page_state_t s_guide_page_state = GUIDE_PAGE_STATE_WELCOME; ///< 当前 Guide 页面状态。
static uint32_t s_welcome_seconds_left = GUIDE_WELCOME_SECONDS;          ///< 欢迎页倒计时剩余秒数。
static uint32_t s_welcome_timer_id = 0;                                  ///< 欢迎页倒计时定时器 ID。
static uint32_t s_welcome_timer_generation = 0;                          ///< 欢迎页倒计时定时器代次。
static system_timer_autodestroy_cb_t s_welcome_tick_cb = NULL;           ///< 欢迎页倒计时回调。
static lv_obj_t* s_welcome_title = NULL;                                 ///< 欢迎页标题标签。
static lv_obj_t* s_welcome_body = NULL;                                  ///< 欢迎页说明标签。
static lv_obj_t* s_welcome_countdown = NULL;                             ///< 欢迎页倒计时标签。
static lv_obj_t* s_welcome_progress = NULL;                              ///< 欢迎页倒计时进度条。
static msgbox_t* s_resume_msgbox = NULL;                                  ///< 断联重连后的继续教学选择框。
static bool s_guide_msg_registered = false;                              ///< Guide 消息是否已注册。

/**
 * @brief 统一设置 Guide 文本标签样式。
 * @param[in] label 目标标签对象。
 * @param[in] font_size 期望字体大小。
 * @return 无返回值。
 */
static void guide_label_apply(lv_obj_t* label, uint32_t font_size) {
    const lv_font_t* font = get_font_by_size_near(font_size);

    floatair_assert(label != NULL, "guide label NULL");
    floatair_assert(font != NULL, "guide font NULL");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    obj_set_text_font(label, font);
}

/**
 * @brief 创建 Guide 欢迎页文本标签。
 * @param[in] parent 父对象。
 * @param[in] font_size 期望字体大小。
 * @param[in] height 标签高度。
 * @return 返回创建后的标签对象。
 */
static lv_obj_t* guide_create_label(lv_obj_t* parent, uint32_t font_size, lv_coord_t height) {
    lv_obj_t* label = lv_label_create(parent);

    floatair_assert(label != NULL, "guide label create failed");
    lv_obj_set_size(label, LV_PCT(90), height);
    guide_label_apply(label, font_size);
    return label;
}

/**
 * @brief 判断当前配置是否为中断后的教学步骤。
 * @return `true` 表示需要询问是否继续，`false` 表示正常开始。
 */
static bool guide_has_resume_progress(void) {
    const char* progress = system_config_get_userguide();

    return progress != NULL &&
           strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_FALSE) != 0 &&
           strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_TRUE) != 0;
}

/**
 * @brief 取消欢迎页倒计时并让已派发的旧回调失效。
 * @return 无返回值。
 */
static void guide_cancel_welcome_timer(void) {
    s_welcome_timer_generation++;
    if (s_welcome_timer_id != 0) {
        system_timer_autodestroy_cancel(s_welcome_timer_id);
        s_welcome_timer_id = 0;
    }
}

/**
 * @brief 启动下一次欢迎页倒计时 tick。
 * @param[in] cb 定时器回调。
 * @return `true` 表示启动成功，`false` 表示启动失败。
 */
static bool guide_schedule_welcome_tick(system_timer_autodestroy_cb_t cb) {
    s_welcome_timer_generation++;
    if (!system_timer_autodestroy_start(GUIDE_WELCOME_TICK_MS,
                                        cb,
                                        (void*)(uintptr_t)s_welcome_timer_generation,
                                        &s_welcome_timer_id)) {
        s_welcome_timer_id = 0;
        return false;
    }
    return true;
}

/**
 * @brief 更新欢迎页倒计时文案。
 * @return 无返回值。
 */
static void guide_update_welcome_countdown(void) {
    uint32_t seconds_left = s_welcome_seconds_left;
    if (seconds_left > GUIDE_WELCOME_SECONDS) {
        seconds_left = GUIDE_WELCOME_SECONDS;
    }
    uint32_t elapsed = GUIDE_WELCOME_SECONDS - seconds_left;
    int32_t progress = (int32_t)((elapsed * 100u) / GUIDE_WELCOME_SECONDS);

    if (s_guide_page_state != GUIDE_PAGE_STATE_WELCOME) {
        return;
    }
    if (s_welcome_countdown == NULL || !lv_obj_is_valid(s_welcome_countdown)) {
        return;
    }
    lv_label_set_text_fmt(s_welcome_countdown,
                          app_get_str("BOOT_GUIDE_WELCOME_COUNTDOWN_FMT"),
                          (unsigned)seconds_left);
    if (s_welcome_progress != NULL && lv_obj_is_valid(s_welcome_progress)) {
        lv_bar_set_value(s_welcome_progress, progress, LV_ANIM_OFF);
    }
}

/**
 * @brief 恢复欢迎页倒计时，避免异常失败后停在中间态。
 * @return 无返回值。
 */
static void guide_restore_welcome_countdown(void) {
    s_guide_page_state = GUIDE_PAGE_STATE_WELCOME;
    s_welcome_seconds_left = GUIDE_WELCOME_SECONDS;
    guide_update_welcome_countdown();
    if (s_welcome_tick_cb != NULL && !guide_schedule_welcome_tick(s_welcome_tick_cb)) {
        floatair_warn("guide welcome timer restore failed");
    }
}

/**
 * @brief 按保存的教学进度恢复运行态。
 * @param[in] progress 保存的教学进度字符串。
 * @return `true` 表示成功恢复并路由到 Home，`false` 表示失败。
 */
static bool guide_resume_progress(const char* progress) {
    if (progress == NULL) {
        return false;
    }
    if (strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP1) == 0) {
        guide_runtime_enter_home_step1();
    } else if (strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP2) == 0) {
        guide_runtime_enter_home_step2_success();
    } else if (strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP3) == 0) {
        guide_runtime_enter_home_step3_success();
    } else if (strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP4) == 0) {
        guide_runtime_enter_home_step4_success();
    } else if (strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP5) == 0) {
        guide_runtime_enter_home_step5_wait_assistant();
    } else {
        return false;
    }
    return app_router_set_app(APP_NAME_HOME, APP_ROUTER_ENTRY_LOCAL);
}

/**
 * @brief 处理断联重连后的继续教学选择。
 * @param[in] box 消息框对象。
 * @param[in] key 用户选择。
 * @param[in] user_data 未使用。
 * @return 无返回值。
 */
static void guide_resume_msgbox_on_result(msgbox_t* box, msgbox_key_t key, void* user_data) {
    const char* progress = NULL;

    (void)user_data;
    if (box != NULL) {
        msgbox_destroy(box);
    }
    s_resume_msgbox = NULL;

    if (key == MSGBOX_KEY_RIGHT) {
        progress = system_config_get_userguide();
        guide_cancel_welcome_timer();
        s_guide_page_state = GUIDE_PAGE_STATE_TRANSITIONING;
        if (guide_resume_progress(progress)) {
            return;
        }
        floatair_warn("guide resume progress failed: %s", progress != NULL ? progress : "NULL");
    }

    guide_runtime_reset();
    (void)system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_FALSE);
    guide_restore_welcome_countdown();
}

/**
 * @brief 显示断联重连后的继续教学选择框。
 * @return `true` 表示成功显示，`false` 表示显示失败。
 */
static bool guide_show_resume_msgbox(void) {
    msgbox_cfg_t cfg = msgbox_default_cfg();

    cfg.message = app_get_str("BOOT_GUIDE_RESUME_MESSAGE");
    cfg.left.key = MSGBOX_KEY_LEFT;
    cfg.left.text = app_get_str("BOOT_GUIDE_RESUME_RESTART");
    cfg.right.key = MSGBOX_KEY_RIGHT;
    cfg.right.text = app_get_str("BOOT_GUIDE_RESUME_CONTINUE");
    s_resume_msgbox = msgbox_show_with_cfg(s_resume_msgbox, &cfg);
    if (s_resume_msgbox == NULL) {
        return false;
    }
    msgbox_set_callback(s_resume_msgbox, guide_resume_msgbox_on_result, NULL);
    return true;
}

/**
 * @brief 进入 Home 教学模式第一步。
 * @return 无返回值。
 */
static void guide_start_home_step1(void) {
    if (s_guide_page_state == GUIDE_PAGE_STATE_TRANSITIONING) {
        return;
    }

    guide_cancel_welcome_timer();
    s_welcome_seconds_left = 0;
    guide_update_welcome_countdown();
    s_guide_page_state = GUIDE_PAGE_STATE_TRANSITIONING;
    guide_runtime_enter_home_step1();
    if (!system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_STEP1)) {
        guide_runtime_reset();
        guide_restore_welcome_countdown();
        floatair_err("guide set step1 progress failed");
        return;
    }
    if (!app_router_set_app(APP_NAME_HOME, APP_ROUTER_ENTRY_LOCAL)) {
        guide_runtime_reset();
        (void)system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_FALSE);
        guide_restore_welcome_countdown();
        floatair_err("guide route home step1 failed");
    }
}

/**
 * @brief 欢迎页倒计时回调。
 * @param[in] user_data 用户上下文。
 * @return 无返回值。
 */
static void guide_welcome_timer_cb(void* user_data) {
    uint32_t generation = (uint32_t)(uintptr_t)user_data;

    if (generation != s_welcome_timer_generation || s_guide_page_state != GUIDE_PAGE_STATE_WELCOME) {
        return;
    }
    s_welcome_timer_id = 0;
    if (s_welcome_seconds_left > 0u) {
        s_welcome_seconds_left--;
    }
    if (s_welcome_seconds_left == 0u) {
        guide_update_welcome_countdown();
        guide_start_home_step1();
        return;
    }
    guide_update_welcome_countdown();
    if (!guide_schedule_welcome_tick(guide_welcome_timer_cb)) {
        floatair_warn("guide welcome timer restart failed");
        guide_start_home_step1();
    }
}

/**
 * @brief 启动欢迎页倒计时。
 * @return 无返回值。
 */
static void guide_start_welcome_timer(void) {
    s_welcome_tick_cb = guide_welcome_timer_cb;
    guide_cancel_welcome_timer();
    guide_update_welcome_countdown();
    if (!guide_schedule_welcome_tick(guide_welcome_timer_cb)) {
        floatair_warn("guide welcome timer start failed");
        guide_start_home_step1();
    }
}

/**
 * @brief 创建 Guide 页面内容。
 * @param[in] root 页面根对象。
 * @param[in] data 页面入参。
 * @return 无返回值。
 */
static void guide_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    floatair_assert(root != NULL, "root NULL");

    guide_cancel_welcome_timer();
    s_guide_page_state = GUIDE_PAGE_STATE_WELCOME;
    s_welcome_seconds_left = GUIDE_WELCOME_SECONDS;
    s_welcome_tick_cb = guide_welcome_timer_cb;

    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_welcome_title = guide_create_label(root, GUIDE_FONT_TITLE, LV_SIZE_CONTENT);
    lv_label_set_text(s_welcome_title, app_get_str("BOOT_GUIDE_WELCOME_TITLE"));
    lv_obj_align(s_welcome_title, LV_ALIGN_CENTER, 0, -70);

    s_welcome_body = guide_create_label(root, GUIDE_FONT_BODY, LV_SIZE_CONTENT);
    lv_label_set_text(s_welcome_body, app_get_str("BOOT_GUIDE_WELCOME_BODY"));
    lv_obj_align(s_welcome_body, LV_ALIGN_CENTER, 0, -10);

    s_welcome_countdown = guide_create_label(root, GUIDE_FONT_BODY, LV_SIZE_CONTENT);
    lv_obj_align(s_welcome_countdown, LV_ALIGN_CENTER, 0, 60);

    s_welcome_progress = lv_bar_create(root);
    floatair_assert(s_welcome_progress != NULL, "guide progress create failed");
    lv_obj_set_size(s_welcome_progress, 110, 4);
    lv_obj_align(s_welcome_progress, LV_ALIGN_CENTER, 0, 94);
    lv_bar_set_range(s_welcome_progress, 0, 100);
    lv_obj_set_style_radius(s_welcome_progress, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_welcome_progress, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_welcome_progress, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_welcome_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_welcome_progress, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_welcome_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_welcome_progress, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_welcome_progress, 0, LV_PART_MAIN);

    if (guide_has_resume_progress()) {
        guide_update_welcome_countdown();
        if (!guide_show_resume_msgbox()) {
            floatair_warn("guide resume msgbox show failed, continue saved progress");
            if (!guide_resume_progress(system_config_get_userguide())) {
                guide_restore_welcome_countdown();
            }
        }
    } else {
        guide_start_welcome_timer();
    }
}

/**
 * @brief Guide 页面显示回调。
 * @param[in] root 页面根对象。
 * @return 无返回值。
 */
static void guide_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    system_status_bar_set_mode(true);
}

/**
 * @brief 销毁 Guide 页面运行态。
 * @return 无返回值。
 */
static void guide_page_destroy(void) {
    guide_cancel_welcome_timer();
    if (s_resume_msgbox != NULL) {
        msgbox_destroy(s_resume_msgbox);
        s_resume_msgbox = NULL;
    }

    s_guide_page_state = GUIDE_PAGE_STATE_WELCOME;
    s_welcome_seconds_left = GUIDE_WELCOME_SECONDS;
    s_welcome_title = NULL;
    s_welcome_body = NULL;
    s_welcome_countdown = NULL;
    s_welcome_progress = NULL;
}

/**
 * @brief Guide 消息处理回调。
 * @param[in] node 消息节点。
 * @param[in] msg 消息包。
 * @return `true` 表示已处理，`false` 表示处理失败。
 */
static bool guide_msg_cb(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    if (!msg) {
        return false;
    }
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static app_message_t guide_msg = {
    .id = APP_MSG_ID_GUIDE,
    .name = APP_NAME_GUIDE,
    .cb = guide_msg_cb,
};

/**
 * @brief 仅注册一次 Guide 消息。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
static bool guide_msg_register_once(void) {
    int ret = 0;

    if (s_guide_msg_registered) {
        return true;
    }

    ret = app_msg_register(&guide_msg);
    if (ret != 0) {
        return false;
    }
    s_guide_msg_registered = true;
    return true;
}

/**
 * @brief 如有需要则注销 Guide 消息。
 * @return 无返回值。
 */
static void guide_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_guide_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_GUIDE);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_guide_msg_registered = false;
}

static app_page_t s_guide_page = {
    .name = APP_NAME_GUIDE,
    .on_create = guide_page_create,
    .on_appear = guide_page_appear,
    .on_disappear = NULL,
    .on_destroy = guide_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* guide_page_get(void) {
    return &s_guide_page;
}

static void guide_app_on_start(void) {
    if (!guide_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)guide_page_get(), NULL, 0)) {
        floatair_assert(false, "guide page replace failed");
        return;
    }
    if (app_router_get_entry_mode() == APP_ROUTER_ENTRY_LOCAL) {
        (void)system_report_guide_open();
    }
}

static void guide_app_on_stop(void) {
    guide_msg_unregister_if_needed();
    guide_page_destroy();
}

static app_t s_guide_app = {
    .name = APP_NAME_GUIDE,
    .on_start = guide_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = guide_app_on_stop,
    .on_back = NULL,
};

bool guide_app_register(void) {
    return app_manager_register(&s_guide_app);
}
