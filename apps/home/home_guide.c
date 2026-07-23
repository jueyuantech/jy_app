/**
 * @file home_guide.c
 * @brief Home 新手教学页面逻辑实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-07-01
 * @copyright JYTek
 * @ingroup app_home
 */
#include "home_guide.h"

#include "app_def.h"
#include "common/app_framework/app_layers.h"
#include "common/app_framework/app_router.h"
#include "floatair_dbg.h"
#include "guide_runtime.h"
#include "home.h"
#include "system/popups/assistant/assistant.h"
#include "system/popups/notify/notify.h"
#include "system/system.h"
#include "system/system_runtime_input.h"
#include "sys_adapter.h"
#include "ui_res.h"

#include <string.h>

#define HOME_GUIDE_NOTIFY_DURATION_MS 3000u ///< Home 教学成功通知显示时长。
#define HOME_GUIDE_TEXT_WIDTH_PCT 92        ///< Home 教学文案宽度百分比。
#define HOME_GUIDE_STEP2_IMG_CENTER_Y (-42) ///< Home 教学第 2 步手势图垂直居中偏移。
#define HOME_GUIDE_STEP2_TEXT_CENTER_Y 32    ///< Home 教学第 2 步文案垂直居中偏移。
#define HOME_GUIDE_STEP2_LANG_Y 28          ///< Home 教学第 2 步语言栏顶部偏移。
#define HOME_GUIDE_STEP2_LANG_LABEL_H 32    ///< Home 教学第 2 步语言标签高度。
#define HOME_GUIDE_STEP2_LANG_LABEL_MIN_W 96  ///< Home 教学第 2 步语言标签最小宽度。
#define HOME_GUIDE_STEP2_LANG_SIDE_PADDING 12 ///< Home 教学第 2 步语言栏左右安全边距。
#define HOME_GUIDE_STEP2_LANG_SWITCH_FALLBACK_W 24 ///< Home 教学第 2 步切换图标默认宽度。
#define HOME_GUIDE_STEP2_LANG_GAP 8         ///< Home 教学第 2 步语言栏间距。
#define HOME_GUIDE_STEP2_LANG_PAD_HOR 8     ///< Home 教学第 2 步语言标签水平内边距。
#define HOME_GUIDE_STEP2_LANG_BORDER_W 1    ///< Home 教学第 2 步语言标签边框宽度。
#define HOME_GUIDE_STEP2_LANG_SOURCE_KEY "BOOT_GUIDE_STEP2_LANG_SOURCE" ///< Home 教学第 2 步源语言文案键。
#define HOME_GUIDE_STEP2_LANG_TARGET_KEY "BOOT_GUIDE_STEP2_LANG_TARGET" ///< Home 教学第 2 步目标语言文案键。

static lv_obj_t* s_guide_img = NULL;      ///< Home 教学手势示意图。
static lv_obj_t* s_guide_title = NULL;    ///< Home 教学主文案。
static lv_obj_t* s_guide_subtitle = NULL; ///< Home 教学辅助文案。
static lv_obj_t* s_step2_mask = NULL;     ///< Home 教学第 2 步遮罩。
static lv_obj_t* s_step2_lang_row = NULL; ///< Home 教学第 2 步语言栏容器。
static lv_obj_t* s_step2_lang_source = NULL; ///< Home 教学第 2 步源语言标签。
static lv_obj_t* s_step2_lang_switch = NULL; ///< Home 教学第 2 步语言切换图标。
static lv_obj_t* s_step2_lang_target = NULL; ///< Home 教学第 2 步目标语言标签。
static lv_obj_t* s_step2_img = NULL;      ///< Home 教学第 2 步手势示意图。
static lv_obj_t* s_step2_title = NULL;    ///< Home 教学第 2 步主文案。
static const lv_font_t* s_step2_lang_font = NULL; ///< Home 教学第 2 步语言标签字体。
static home_guide_ops_t s_ops = {0};      ///< Home 教学异步事件使用的页面动作。

/**
 * @brief 展示 Home 教学消息通知。
 * @param[in] title 通知标题。
 * @return 无返回值。
 */
static void home_guide_show_message_notify(const char* title) {
    notify_cfg_t cfg = notify_default_cfg();
    notify_t* notify = NULL;

    cfg.title = title;
    cfg.image_src = UI_RES_IMAGE_IM_MESSAGE;
    cfg.image_src_size = 0;
    cfg.mode = NOTIFY_MODE_MESSAGE;
    cfg.duration_ms = HOME_GUIDE_NOTIFY_DURATION_MS;
    notify = notify_show_with_cfg(&cfg);
    notify_set_body_hint_visible(notify, false);
}

/**
 * @brief 设置 Home 教学第 2 步语言标签样式。
 * @param[in] label 目标语言标签。
 * @param[in] text 标签文本。
 * @param[in] font 标签字体。
 * @return 无返回值。
 */
static void home_guide_step2_init_lang_label(lv_obj_t* label, const char* text, const lv_font_t* font) {
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, text);
    obj_set_text_font(label, font);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(label, HOME_GUIDE_STEP2_LANG_LABEL_H, 0);
    lv_obj_set_style_min_width(label, HOME_GUIDE_STEP2_LANG_LABEL_MIN_W, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_radius(label, 4, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 1, 0);
    lv_obj_set_style_border_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(label, lv_color_white(), 0);
    lv_obj_set_style_pad_hor(label, HOME_GUIDE_STEP2_LANG_PAD_HOR, 0);
    lv_obj_set_style_pad_ver(label, 4, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

/**
 * @brief 获取 Home 教学第 2 步语言标签按内容展示所需宽度。
 * @param[in] label 目标语言标签。
 * @return 返回包含内边距和边框后的内容宽度。
 */
static lv_coord_t home_guide_step2_lang_natural_width(lv_obj_t* label) {
    const char* text = NULL;
    lv_coord_t text_width = 0;

    if (label == NULL || !lv_obj_is_valid(label)) {
        return HOME_GUIDE_STEP2_LANG_LABEL_MIN_W;
    }

    text = lv_label_get_text(label);
    if (text != NULL && s_step2_lang_font != NULL) {
        text_width = (lv_coord_t)lv_text_get_width(text, (uint32_t)strlen(text), s_step2_lang_font, 0);
    }
    text_width += (HOME_GUIDE_STEP2_LANG_PAD_HOR * 2) + (HOME_GUIDE_STEP2_LANG_BORDER_W * 2);
    if (text_width < HOME_GUIDE_STEP2_LANG_LABEL_MIN_W) {
        text_width = HOME_GUIDE_STEP2_LANG_LABEL_MIN_W;
    }
    return text_width;
}

/**
 * @brief 按当前屏幕宽度同步 Home 教学第 2 步语言栏尺寸。
 * @return 无返回值。
 */
static void home_guide_step2_sync_lang_layout(void) {
    lv_coord_t row_width = (lv_coord_t)config_lcd.ui_width - (HOME_GUIDE_STEP2_LANG_SIDE_PADDING * 2);
    lv_coord_t switch_width = HOME_GUIDE_STEP2_LANG_SWITCH_FALLBACK_W;
    lv_coord_t label_space = 0;
    lv_coord_t source_width = 0;
    lv_coord_t target_width = 0;
    lv_coord_t label_total_width = 0;

    if (s_step2_lang_row == NULL || !lv_obj_is_valid(s_step2_lang_row)) {
        return;
    }

    if (row_width < HOME_GUIDE_STEP2_LANG_LABEL_MIN_W) {
        row_width = (lv_coord_t)config_lcd.ui_width;
    }
    if (s_step2_lang_switch != NULL && lv_obj_is_valid(s_step2_lang_switch)) {
        switch_width = lv_obj_get_width(s_step2_lang_switch);
        if (switch_width <= 0) {
            switch_width = HOME_GUIDE_STEP2_LANG_SWITCH_FALLBACK_W;
        }
    }
    label_space = row_width - switch_width - (HOME_GUIDE_STEP2_LANG_GAP * 2);
    if (label_space < 2) {
        label_space = 2;
    }

    source_width = home_guide_step2_lang_natural_width(s_step2_lang_source);
    target_width = home_guide_step2_lang_natural_width(s_step2_lang_target);
    label_total_width = source_width + target_width;
    if (label_total_width > label_space) {
        source_width = (lv_coord_t)((int32_t)label_space * source_width / label_total_width);
        target_width = label_space - source_width;
        if (source_width < 1) {
            source_width = 1;
        }
        if (target_width < 1) {
            target_width = 1;
        }
    }

    if (s_step2_lang_source != NULL && lv_obj_is_valid(s_step2_lang_source)) {
        lv_obj_set_width(s_step2_lang_source, source_width);
    }
    if (s_step2_lang_target != NULL && lv_obj_is_valid(s_step2_lang_target)) {
        lv_obj_set_width(s_step2_lang_target, target_width);
    }
    lv_obj_set_width(s_step2_lang_row, row_width);
    lv_obj_align(s_step2_lang_row, LV_ALIGN_TOP_MID, 0, HOME_GUIDE_STEP2_LANG_Y);
}

/**
 * @brief 进入教学步骤 3 的向后滑动提示。
 * @return 无返回值。
 */
static void home_guide_enter_step3(void) {
    if (!guide_runtime_is_home_step2_success()) {
        return;
    }
    guide_runtime_enter_home_step3_wait_back();
    home_guide_layout_update();
}

/**
 * @brief 进入教学步骤 4 的息屏提示。
 * @return 无返回值。
 */
static void home_guide_enter_step4(void) {
    if (!guide_runtime_is_home_step3_success()) {
        return;
    }
    guide_runtime_enter_home_step4_wait_sleep();
    home_guide_layout_update();
}

/**
 * @brief 进入教学步骤 5 的语音唤醒提示。
 * @param[in] ops Home 页面动作回调。
 * @return 无返回值。
 */
static void home_guide_enter_step5(const home_guide_ops_t* ops) {
    if (!guide_runtime_is_home_step4_success()) {
        return;
    }
    guide_runtime_enter_home_step5_wait_assistant();
    (void)system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_STEP5);
    if (ops != NULL && ops->refresh != NULL) {
        ops->refresh();
    } else if (s_ops.refresh != NULL) {
        s_ops.refresh();
    } else {
        home_guide_layout_update();
    }
}

/**
 * @brief 进入教学步骤 4 的已息屏状态。
 * @return 无返回值。
 */
static void home_guide_enter_step4_sleeping(void) {
    if (!guide_runtime_is_home_step4_wait_sleep()) {
        return;
    }
    guide_runtime_enter_home_step4_sleeping();
    home_guide_layout_update();
}

/**
 * @brief 完成教学步骤 4 的亮屏动作。
 * @return 无返回值。
 */
static void home_guide_complete_step4(void) {
    if (!guide_runtime_is_home_step4_sleeping()) {
        return;
    }
    guide_runtime_enter_home_step4_success();
    (void)system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_STEP4);
    home_guide_enter_step5(&s_ops);
    home_guide_show_message_notify(app_get_str("BOOT_GUIDE_NOTIFY_SCREEN_DONE"));
}

/**
 * @brief 完成或跳过教学步骤 5，并进入真实 Home。
 * @param[in] skipped 是否为用户主动跳过。
 * @param[in] ops Home 页面动作回调。
 * @return 无返回值。
 */
static void home_guide_finish_step5(bool skipped, const home_guide_ops_t* ops) {
    if (!guide_runtime_is_home_step5_wait_assistant()) {
        return;
    }
    guide_runtime_reset();
    (void)system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_TRUE);
    (void)system_report_guide_close();
    if (ops != NULL && ops->reset_selection != NULL) {
        ops->reset_selection();
    } else if (s_ops.reset_selection != NULL) {
        s_ops.reset_selection();
    }
    home_guide_show_message_notify(skipped
                                   ? app_get_str("BOOT_GUIDE_NOTIFY_SKIPPED")
                                   : app_get_str("BOOT_GUIDE_NOTIFY_WAKE_DONE"));
    if (!app_router_call_home()) {
        floatair_warn("route after userguide finish failed");
    } else if (strcmp(app_router_get_app(), APP_NAME_HOME) == 0) {
        (void)system_report_view_change(APP_NAME_HOME);
    }
}

/**
 * @brief 处理系统亮灭屏状态变化，用于教学步骤 4。
 * @param[in] event LVGL 事件对象。
 * @return 无返回值。
 */
static void home_guide_sys_state_event_handle(lv_event_t* event) {
    const uint8_t* state = (const uint8_t*)lv_event_get_param(event);

    if (state == NULL) {
        return;
    }
    if (*state == 0 && guide_runtime_is_home_step4_wait_sleep()) {
        home_guide_enter_step4_sleeping();
        return;
    }
    if (*state != 0 && guide_runtime_is_home_step4_sleeping()) {
        home_guide_complete_step4();
    }
}

/**
 * @brief 处理 assistant closeAssistant 事件，用于教学步骤 5 完成。
 * @param[in] event LVGL 事件对象。
 * @return 无返回值。
 */
static void home_guide_assistant_close_event_handle(lv_event_t* event) {
    (void)event;
    home_guide_finish_step5(false, &s_ops);
}

void home_guide_create_controls(lv_obj_t* parent, const lv_font_t* font, int font_height) {
    (void)font_height;
    lv_obj_t* step2_parent = home_enable_app_float ? app_layers_get_app_float() : parent;

    if (step2_parent == NULL || !lv_obj_is_valid(step2_parent)) {
        step2_parent = parent;
    }

    s_step2_lang_font = font;

    s_guide_img = lv_image_create(parent);
    floatair_assert(s_guide_img != NULL, "guide_step1_img NULL");
    lv_image_set_src(s_guide_img, UI_RES_IMAGE_GUIDE_STEP_1);
    lv_obj_add_flag(s_guide_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_null_on_delete(&s_guide_img);

    s_guide_title = lv_label_create(parent);
    floatair_assert(s_guide_title != NULL, "guide_step1_title NULL");
    lv_obj_set_size(s_guide_title, LV_PCT(HOME_GUIDE_TEXT_WIDTH_PCT), LV_SIZE_CONTENT);
    lv_label_set_text(s_guide_title, app_get_str("BOOT_GUIDE_STEP1_TITLE"));
    obj_set_text_font(s_guide_title, font);
    lv_obj_set_style_text_color(s_guide_title, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_guide_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_guide_title, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_guide_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_null_on_delete(&s_guide_title);

    s_guide_subtitle = lv_label_create(parent);
    floatair_assert(s_guide_subtitle != NULL, "guide_step1_subtitle NULL");
    lv_obj_set_size(s_guide_subtitle, LV_PCT(HOME_GUIDE_TEXT_WIDTH_PCT), LV_SIZE_CONTENT);
    lv_label_set_text(s_guide_subtitle, app_get_str("BOOT_GUIDE_STEP1_SUBTITLE"));
    obj_set_text_font(s_guide_subtitle, font);
    lv_obj_set_style_text_color(s_guide_subtitle, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_guide_subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_guide_subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_guide_subtitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_null_on_delete(&s_guide_subtitle);

    s_step2_mask = lv_obj_create(step2_parent);
    floatair_assert(s_step2_mask != NULL, "guide_step2_mask NULL");
    lv_obj_remove_style_all(s_step2_mask);
    lv_obj_set_size(s_step2_mask, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_step2_mask, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_step2_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_step2_mask, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_step2_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_step2_mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_step2_mask, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(s_step2_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_null_on_delete(&s_step2_mask);

    s_step2_lang_row = lv_obj_create(s_step2_mask);
    floatair_assert(s_step2_lang_row != NULL, "guide_step2_lang_row NULL");
    lv_obj_remove_style_all(s_step2_lang_row);
    lv_obj_set_size(s_step2_lang_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(s_step2_lang_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_step2_lang_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_step2_lang_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_step2_lang_row, HOME_GUIDE_STEP2_LANG_GAP, 0);
    lv_obj_align(s_step2_lang_row, LV_ALIGN_TOP_MID, 0, HOME_GUIDE_STEP2_LANG_Y);
    lv_obj_null_on_delete(&s_step2_lang_row);

    s_step2_lang_source = lv_label_create(s_step2_lang_row);
    floatair_assert(s_step2_lang_source != NULL, "guide_step2_lang_source NULL");
    home_guide_step2_init_lang_label(s_step2_lang_source, app_get_str(HOME_GUIDE_STEP2_LANG_SOURCE_KEY), font);
    lv_obj_null_on_delete(&s_step2_lang_source);

    s_step2_lang_switch = lv_image_create(s_step2_lang_row);
    floatair_assert(s_step2_lang_switch != NULL, "guide_step2_lang_switch NULL");
    lv_image_set_src(s_step2_lang_switch, UI_RES_IMAGE_SWITCH);
    lv_obj_null_on_delete(&s_step2_lang_switch);

    s_step2_lang_target = lv_label_create(s_step2_lang_row);
    floatair_assert(s_step2_lang_target != NULL, "guide_step2_lang_target NULL");
    home_guide_step2_init_lang_label(s_step2_lang_target, app_get_str(HOME_GUIDE_STEP2_LANG_TARGET_KEY), font);
    lv_obj_null_on_delete(&s_step2_lang_target);

    s_step2_img = lv_image_create(s_step2_mask);
    floatair_assert(s_step2_img != NULL, "guide_step2_img NULL");
    lv_image_set_src(s_step2_img, UI_RES_IMAGE_GUIDE_STEP_2);
    lv_obj_align(s_step2_img, LV_ALIGN_CENTER, 0, HOME_GUIDE_STEP2_IMG_CENTER_Y);
    lv_obj_null_on_delete(&s_step2_img);

    s_step2_title = lv_label_create(s_step2_mask);
    floatair_assert(s_step2_title != NULL, "guide_step2_title NULL");
    lv_obj_set_width(s_step2_title, LV_PCT(HOME_GUIDE_TEXT_WIDTH_PCT));
    lv_label_set_text(s_step2_title, app_get_str("BOOT_GUIDE_STEP2_BODY"));
    obj_set_text_font(s_step2_title, font);
    lv_obj_set_style_text_color(s_step2_title, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_step2_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_step2_title, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_step2_title, LV_ALIGN_CENTER, 0, HOME_GUIDE_STEP2_TEXT_CENTER_Y);
    lv_obj_null_on_delete(&s_step2_title);
}

/**
 * @brief 销毁 Home 教学中可能挂载到页面外部浮层的控件。
 * @return 无返回值。
 */
void home_guide_destroy_controls(void) {
    if (s_step2_mask != NULL && lv_obj_is_valid(s_step2_mask)) {
        lv_obj_delete(s_step2_mask);
    }
    s_step2_mask = NULL;
    s_step2_lang_row = NULL;
    s_step2_lang_source = NULL;
    s_step2_lang_switch = NULL;
    s_step2_lang_target = NULL;
    s_step2_img = NULL;
    s_step2_title = NULL;
    s_step2_lang_font = NULL;
}

void home_guide_layout_update(void) {
    bool show_step1 = guide_runtime_is_home_step1();
    bool show_step2 = guide_runtime_is_home_step2_wait_back();
    bool show_step3_back = guide_runtime_is_home_step3_wait_back();
    bool show_step3_forward = guide_runtime_is_home_step3_wait_forward();
    bool show_step4 = guide_runtime_is_home_step4_wait_sleep();
    bool show_step5 = guide_runtime_is_home_step5_wait_assistant();
    bool show_guide = show_step1 || show_step3_back || show_step3_forward || show_step4 || show_step5;

    if (s_step2_mask != NULL && lv_obj_is_valid(s_step2_mask)) {
        if (show_step2) {
            home_guide_step2_sync_lang_layout();
            lv_obj_clear_flag(s_step2_mask, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_step2_mask);
        } else {
            lv_obj_add_flag(s_step2_mask, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_guide_img != NULL && lv_obj_is_valid(s_guide_img)) {
        if (show_guide && !show_step5) {
            const char* image_src = UI_RES_IMAGE_GUIDE_STEP_1;

            if (show_step3_back || show_step3_forward) {
                image_src = UI_RES_IMAGE_GUIDE_STEP_3;
            } else if (show_step4) {
                image_src = UI_RES_IMAGE_GUIDE_STEP_4;
            }
            lv_obj_clear_flag(s_guide_img, LV_OBJ_FLAG_HIDDEN);
            lv_image_set_src(s_guide_img, image_src);
            lv_obj_align(s_guide_img, LV_ALIGN_TOP_MID, 0, 36);
        } else {
            lv_obj_add_flag(s_guide_img, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_guide_title != NULL && lv_obj_is_valid(s_guide_title)) {
        if (show_guide) {
            lv_obj_clear_flag(s_guide_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(s_guide_title, LV_PCT(HOME_GUIDE_TEXT_WIDTH_PCT));
            lv_obj_set_height(s_guide_title, LV_SIZE_CONTENT);
            if (show_step1) {
                lv_label_set_text(s_guide_title, app_get_str("BOOT_GUIDE_STEP1_BODY"));
            } else if (show_step3_back) {
                lv_label_set_text(s_guide_title, app_get_str("BOOT_GUIDE_STEP3_FORWARD_BODY"));
            } else if (show_step3_forward) {
                lv_label_set_text(s_guide_title, app_get_str("BOOT_GUIDE_STEP3_BACK_BODY"));
            } else if (show_step4) {
                lv_label_set_text(s_guide_title, app_get_str("BOOT_GUIDE_STEP4_BODY"));
            } else {
                lv_label_set_text(s_guide_title, app_get_str("BOOT_GUIDE_STEP5_BODY"));
            }
            lv_obj_align(s_guide_title, LV_ALIGN_TOP_MID, 0, show_step5 ? 48 : 108);
        } else {
            lv_obj_set_height(s_guide_title, get_font_height(get_system_font()));
            lv_obj_add_flag(s_guide_title, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_guide_subtitle != NULL && lv_obj_is_valid(s_guide_subtitle)) {
        if (show_step5) {
            lv_obj_clear_flag(s_guide_subtitle, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(s_guide_subtitle, LV_PCT(HOME_GUIDE_TEXT_WIDTH_PCT));
            lv_label_set_text(s_guide_subtitle, app_get_str("BOOT_GUIDE_STEP5_SKIP_HINT"));
            lv_obj_align(s_guide_subtitle, LV_ALIGN_TOP_MID, 0, 116);
        } else {
            lv_obj_add_flag(s_guide_subtitle, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

bool home_guide_apply_step1_selection(bool (*select_app_by_name)(const char*),
                                      bool view_ready,
                                      void (*refresh)(void)) {
    if (!guide_runtime_is_home_step1()) {
        return false;
    }
    if (select_app_by_name == NULL || !select_app_by_name(APP_NAME_TRANSLATE)) {
        floatair_warn("guide step1 translate app missing in home units");
        return true;
    }
    if (view_ready && refresh != NULL) {
        refresh();
    }
    return true;
}

bool home_guide_is_step1(void) {
    return guide_runtime_is_home_step1();
}

bool home_guide_handle_touch(lv_event_code_t code, const home_guide_ops_t* ops) {
    if (guide_runtime_is_home_step1()) {
        if (code == LV_EVENT_CLICKED) {
            guide_runtime_enter_home_step2_wait_back();
            if (ops != NULL && ops->refresh != NULL) {
                ops->refresh();
            } else {
                home_guide_layout_update();
            }
            home_guide_show_message_notify(app_get_str("BOOT_GUIDE_NOTIFY_ENTERED"));
        }
        return true;
    }
    if (guide_runtime_is_home_step2_wait_back()) {
        if (code == LV_EVENT_DCLICKED) {
            guide_runtime_enter_home_step2_success();
            (void)system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_STEP2);
            home_guide_enter_step3();
            home_guide_show_message_notify(app_get_str("BOOT_GUIDE_NOTIFY_RETURNED"));
        }
        return true;
    }
    if (guide_runtime_is_home_step2_success()) {
        return true;
    }
    if (guide_runtime_is_home_step3_success() || guide_runtime_is_home_step4_success()) {
        return true;
    }
    if (guide_runtime_is_home_step3_wait_back()) {
        if (code == LV_EVENT_GESTURE_LEFT && ops != NULL && ops->screen_swipe_left != NULL) {
            ops->screen_swipe_left();
            guide_runtime_enter_home_step3_wait_forward();
            home_guide_layout_update();
        }
        return true;
    }
    if (guide_runtime_is_home_step3_wait_forward()) {
        if (code == LV_EVENT_GESTURE_RIGHT && ops != NULL && ops->screen_swipe_right != NULL) {
            ops->screen_swipe_right();
            guide_runtime_enter_home_step3_success();
            (void)system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_STEP3);
            home_guide_enter_step4();
            home_guide_show_message_notify(app_get_str("BOOT_GUIDE_NOTIFY_MENU_BACK"));
        }
        return true;
    }
    if (guide_runtime_is_home_step4_wait_sleep()) {
        return true;
    }
    if (guide_runtime_is_home_step4_sleeping()) {
        return true;
    }
    if (guide_runtime_is_home_step5_wait_assistant()) {
        if (code == LV_EVENT_DCLICKED) {
            home_guide_finish_step5(true, ops);
        }
        return true;
    }
    return false;
}

void home_guide_set_ops(const home_guide_ops_t* ops) {
    if (ops == NULL) {
        memset(&s_ops, 0, sizeof(s_ops));
        return;
    }
    s_ops = *ops;
}

void home_guide_register_events(lv_obj_t* root) {
    lv_obj_add_event_cb(root,
                        home_guide_sys_state_event_handle,
                        system_runtime_input_get_sys_state_event(),
                        NULL);
    lv_obj_add_event_cb(root, home_guide_assistant_close_event_handle, assistant_get_close_event(), NULL);
}

void home_guide_on_appear(void) {
    if (guide_runtime_is_home_step2_wait_back()) {
        home_guide_layout_update();
    } else if (guide_runtime_is_home_step2_success()) {
        home_guide_enter_step3();
        home_guide_show_message_notify(app_get_str("BOOT_GUIDE_NOTIFY_RETURNED"));
    } else if (guide_runtime_is_home_step3_success()) {
        home_guide_enter_step4();
        home_guide_show_message_notify(app_get_str("BOOT_GUIDE_NOTIFY_MENU_BACK"));
    } else if (guide_runtime_is_home_step4_success()) {
        home_guide_enter_step5(&s_ops);
        home_guide_show_message_notify(app_get_str("BOOT_GUIDE_NOTIFY_SCREEN_DONE"));
    }
}
