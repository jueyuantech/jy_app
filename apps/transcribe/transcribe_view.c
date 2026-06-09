/**
 * @file transcribe_view.c
 * @brief 转写应用页面视图实现
 */
#include "home/home.h"
#include "stt_view_common.h"
#include "transcribe.h"

#include "common/app_framework/app_manager.h"
#include "common/widgets/container.h"
#include "common/widgets/img.h"
#include "common/widgets/label.h"
#include "common/widgets/status_bar.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_def.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"
#include "floatair_fs.h"

#include <string.h>

/**
 * @brief 转写页面当前展示模式。
 */
typedef enum {
    VIEW_MODE_FUNC_NONE = 0,        ///< 页面内容全部隐藏。
    VIEW_MODE_FUNC_DESCRIPTION = 1, ///< 展示功能说明态。
    VIEW_MODE_FUNC_WAIT = 2,        ///< 展示等待连接态。
    VIEW_MODE_FUNC_STT = 3,         ///< 展示实时转写态。
} view_mode_t;

#define TRANSCRIBE_TEXT_SIDE_PADDING 12
#define TRANSCRIBE_SCROLL_TOP_MARGIN 2
#define TRANSCRIBE_LANG_HINT_PADDING LVGL_UI_MARGIN_10

/**
 * @brief 转写页单条 STT 行视图缓存。
 */
typedef struct {
    container_t* row; ///< 行根容器。
    label_t* label;   ///< 转写文本标签。
} transcribe_stt_row_t;

static label_t* transcribe_init_label = NULL;
static img_t* transcribe_init_img = NULL;
static label_t* transcribe_notice_op = NULL;
static lv_obj_t* transcribe_audio_source = NULL;
static lv_obj_t* transcribe_mic_direction = NULL;
static lv_obj_t* transcribe_waveicon = NULL;
static lv_obj_t* transcribe_root = NULL;
static container_t* transcribe_content = NULL;
static label_t* transcribe_lang = NULL;
static container_t* transcribe_scroll = NULL;
static container_t* transcribe_scroll_spacer = NULL;
static transcribe_stt_row_t transcribe_stt_rows[STT_INFO_MAX_MSG_NUM];
static view_mode_t transcribe_mode = VIEW_MODE_FUNC_NONE;

static bool transcribe_status_bar_widgets_valid(void) {
    return transcribe_audio_source != NULL &&
           lv_obj_is_valid(transcribe_audio_source) &&
           transcribe_waveicon != NULL &&
           lv_obj_is_valid(transcribe_waveicon) &&
           transcribe_mic_direction != NULL &&
           lv_obj_is_valid(transcribe_mic_direction);
}

static bool transcribe_status_bar_ensure_widgets(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        transcribe_audio_source = NULL;
        transcribe_waveicon = NULL;
        transcribe_mic_direction = NULL;
        return false;
    }
    if (!transcribe_status_bar_widgets_valid()) {
        status_bar_clear_custom_widgets(status_bar);
        transcribe_audio_source = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("sound_phone.jpg"), STATUS_BAR_WIDGET_ALIGN_LEFT);
        transcribe_waveicon = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("sound_wave.jpg"), STATUS_BAR_WIDGET_ALIGN_RIGHT);
        transcribe_mic_direction = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("micphone.jpg"), STATUS_BAR_WIDGET_ALIGN_RIGHT);
    }

    return transcribe_status_bar_widgets_valid();
}

/**
 * @brief 切换到空白模式。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void transcribe_mode_go_none(void) {
    transcribe_mode = VIEW_MODE_FUNC_NONE;
    ui_widget_set_visible(UI_WIDGET(transcribe_init_label), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_init_img), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_notice_op), false);
    if (transcribe_audio_source != NULL && lv_obj_is_valid(transcribe_audio_source)) {
        lv_obj_add_flag(transcribe_audio_source, LV_OBJ_FLAG_HIDDEN);
    }
    if (transcribe_mic_direction != NULL && lv_obj_is_valid(transcribe_mic_direction)) {
        lv_obj_add_flag(transcribe_mic_direction, LV_OBJ_FLAG_HIDDEN);
    }
    ui_widget_set_visible(UI_WIDGET(transcribe_content), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_lang), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_scroll), false);
    stt_view_update_waveicon(transcribe_waveicon, false);
}

/**
 * @brief 切换到功能说明模式。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void transcribe_mode_go_description(void) {
    transcribe_mode = VIEW_MODE_FUNC_DESCRIPTION;
    ui_widget_set_visible(UI_WIDGET(transcribe_init_label), true);
    if (transcribe_init_label != NULL) {
        lv_label_set_text_fmt(label_get_obj(transcribe_init_label),
                              "%s%s",
                              app_get_str("IDLE_ASR"),
                              app_get_str("SYSTEM_APP"));
    }
    ui_widget_set_visible(UI_WIDGET(transcribe_init_img), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_notice_op), true);
    if (transcribe_notice_op != NULL) {
        lv_label_set_text_fmt(label_get_obj(transcribe_notice_op),
                              "%s%s%s",
                              app_get_str("SYSTEM_LP_TOUCH"),
                              app_get_str("SYSTEM_LP_OPEN"),
                              app_get_str("IDLE_ASR"));
        lv_obj_update_layout(label_get_obj(transcribe_notice_op));
    }
    stt_view_update_audio_source(transcribe_audio_source);
    stt_view_update_mic_direction(transcribe_mic_direction);
    ui_widget_set_visible(UI_WIDGET(transcribe_content), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_lang), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_scroll), false);
    stt_view_update_waveicon(transcribe_waveicon, false);
}

/**
 * @brief 切换到等待连接模式。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void transcribe_mode_go_wait(void) {
    transcribe_mode = VIEW_MODE_FUNC_WAIT;
    ui_widget_set_visible(UI_WIDGET(transcribe_init_label), true);
    if (transcribe_init_label != NULL) {
        lv_label_set_text_fmt(label_get_obj(transcribe_init_label),
                              "%s%s",
                              app_get_str("IDLE_ASR"),
                              app_get_str("SYSTEM_OPENIGN"));
    }
    ui_widget_set_visible(UI_WIDGET(transcribe_init_img), true);
    if (transcribe_init_img != NULL) {
        img_set_src(transcribe_init_img, FLOATAIR_SYS_IMG("connecting.jpg"));
    }
    ui_widget_set_visible(UI_WIDGET(transcribe_notice_op), false);
    stt_view_update_audio_source(transcribe_audio_source);
    stt_view_update_mic_direction(transcribe_mic_direction);
    ui_widget_set_visible(UI_WIDGET(transcribe_content), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_lang), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_scroll), false);
    stt_view_update_waveicon(transcribe_waveicon, false);
}

/**
 * @brief 切换到 STT 展示模式。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void transcribe_mode_go_stt(void) {
    transcribe_mode = VIEW_MODE_FUNC_STT;
    ui_widget_set_visible(UI_WIDGET(transcribe_init_label), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_init_img), false);
    ui_widget_set_visible(UI_WIDGET(transcribe_notice_op), false);
    stt_view_update_audio_source(transcribe_audio_source);
    stt_view_update_mic_direction(transcribe_mic_direction);
    ui_widget_set_visible(UI_WIDGET(transcribe_content), true);
    transcribe_update_lang_hint();
    ui_widget_set_visible(UI_WIDGET(transcribe_scroll), true);
    if (transcribe_content != NULL) {
        lv_obj_update_layout(container_get_obj(transcribe_content));
    }
    stt_view_update_waveicon(transcribe_waveicon, true);
}

/**
 * @brief 初始化单条 STT 行缓存。
 *
 * @param row 目标行缓存。
 * @param parent 父对象。
 * @return 无返回值。
 */
static void transcribe_stt_init_row(transcribe_stt_row_t* row, lv_obj_t* parent) {
    if (row == NULL || parent == NULL) {
        return;
    }
    if (row->row != NULL) {
        return;
    }

    row->row = stt_view_create_plain_container(parent, LV_PCT(100), LV_SIZE_CONTENT);
    floatair_assert(row->row != NULL, "transcribe stt row NULL");
    container_set_layout_vbox(row->row);
    container_set_align(row->row, CONTAINER_ALIGN_START, CONTAINER_ALIGN_START, CONTAINER_ALIGN_START);
    row->label = NULL;
    ui_widget_set_visible(UI_WIDGET(row->row), false);
}

/**
 * @brief 清空转写页 STT 行内已创建的标签缓存。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void transcribe_stt_reset_row_labels(void) {
    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        lv_obj_t* row_obj = NULL;

        if (transcribe_stt_rows[i].row == NULL) {
            continue;
        }

        row_obj = container_get_obj(transcribe_stt_rows[i].row);
        if (row_obj != NULL) {
            lv_obj_clean(row_obj);
        }
        transcribe_stt_rows[i].label = NULL;
    }
}

/**
 * @brief 按当前数据刷新单条 STT 行内容。
 *
 * @param row 目标行缓存。
 * @param text 文本内容。
 * @param base_dir 文本方向。
 * @param is_bottom_most 是否为当前高亮内容。
 * @return 无返回值。
 */
static void transcribe_stt_update_row(transcribe_stt_row_t* row,
                                      const char* text,
                                      lv_base_dir_t base_dir,
                                      bool is_bottom_most,
                                      bool is_final) {
    uint32_t set_text_start_us = 0;
    uint32_t set_text_cost_us = 0;
    uint32_t layout_start_us = 0;
    uint32_t layout_cost_us = 0;
    lv_obj_t* row_obj = NULL;
    bool has_text = (text != NULL && text[0] != '\0');

    if (row == NULL || row->row == NULL || !has_text) {
        if (row != NULL && row->row != NULL) {
            ui_widget_set_visible(UI_WIDGET(row->row), false);
        }
        return;
    }

    row_obj = container_get_obj(row->row);
    if (row_obj == NULL) {
        ui_widget_set_visible(UI_WIDGET(row->row), false);
        return;
    }

    ui_widget_set_size(UI_WIDGET(row->row), LV_PCT(100), LV_SIZE_CONTENT);
    ui_widget_set_visible(UI_WIDGET(row->row), true);

    if (row->label == NULL) {
        row->label = stt_view_create_text_label(row_obj,
                                                LV_PCT(100),
                                                LV_SIZE_CONTENT,
                                                "",
                                                LABEL_ALIGN_LEFT,
                                                LABEL_OVERFLOW_WRAP);
        floatair_assert(row->label != NULL, "transcribe stt label NULL");
        stt_view_apply_stt_label_text_theme(row->label, base_dir);
    }
    (void)is_final;
    set_text_start_us = (uint32_t)GetTimeUs();
    stt_view_update_incremental_text(row->label, text);
    set_text_cost_us = (uint32_t)GetTimeUs() - set_text_start_us;
    // if (is_final) {
    //     stt_view_update_incremental_text(row->label, text);
    // } else {
    //     stt_view_update_incremental_text_max_128(row->label, text);
    // }
    ui_widget_set_size(UI_WIDGET(row->label), LV_PCT(100), LV_SIZE_CONTENT);
    ui_widget_set_visible(UI_WIDGET(row->label), true);
    stt_view_apply_stt_label_theme(row->label, base_dir, is_bottom_most);
    layout_start_us = (uint32_t)GetTimeUs();
    lv_obj_update_layout(row_obj);
    layout_cost_us = (uint32_t)GetTimeUs() - layout_start_us;
    floatair_info("transcribe row timing: set_text=%luus/%lums layout=%luus/%lums final=%d bottom=%d",
                  (unsigned long)set_text_cost_us,
                  (unsigned long)(set_text_cost_us / 1000U),
                  (unsigned long)layout_cost_us,
                  (unsigned long)(layout_cost_us / 1000U),
                  is_final ? 1 : 0,
                  is_bottom_most ? 1 : 0);
}

/**
 * @brief 创建转写页 STT 行缓存。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void transcribe_stt_init_rows(void) {
    lv_obj_t* scroll_obj = NULL;

    if (transcribe_scroll == NULL) {
        return;
    }

    scroll_obj = container_get_obj(transcribe_scroll);
    if (scroll_obj == NULL) {
        return;
    }

    if (transcribe_scroll_spacer == NULL) {
        transcribe_scroll_spacer = stt_view_create_plain_container(scroll_obj, LV_PCT(100), 0);
        floatair_assert(transcribe_scroll_spacer != NULL, "transcribe_scroll_spacer NULL");
        container_set_child_grow(container_get_obj(transcribe_scroll_spacer), 1);
    }

    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        transcribe_stt_init_row(&transcribe_stt_rows[i], scroll_obj);
    }
}

/**
 * @brief 隐藏所有 STT 行缓存。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void transcribe_stt_hide_all_rows(void) {
    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (transcribe_stt_rows[i].row != NULL) {
            ui_widget_set_visible(UI_WIDGET(transcribe_stt_rows[i].row), false);
        }
    }
}

/**
 * @brief 刷新转写 STT 内容区域。
 *
 * @param 无参数。
 * @return 无返回值。
 */
void transcribe_stt_update(void) {
    uint32_t update_start_us = (uint32_t)GetTimeUs();
    uint32_t update_end_us = 0;
    size_t visible_row_count = 0;

    transcribe_update_lang_hint();

    if (transcribe_scroll == NULL) {
        floatair_info("transcribe_scroll == NULL");
        return;
    }

    if (container_get_obj(transcribe_scroll) == NULL) {
        floatair_info("transcribe_scroll obj == NULL");
        return;
    }

    if (stt_size() == 0) {
        floatair_info("stt_size() == 0");
        transcribe_stt_hide_all_rows();
        ui_widget_set_visible(UI_WIDGET(transcribe_scroll), false);
        if (transcribe_mode != VIEW_MODE_FUNC_NONE) {
            stt_view_update_audio_source(transcribe_audio_source);
            stt_view_update_mic_direction(transcribe_mic_direction);
        }
        return;
    }
    if (transcribe_mode != VIEW_MODE_FUNC_STT) {
        transcribe_mode_go_stt();
    } else {
        stt_view_update_audio_source(transcribe_audio_source);
        stt_view_update_mic_direction(transcribe_mic_direction);
    }

    transcribe_stt_init_rows();

    for (int index = (stt_config.textMode != TEXTMODE_HISTORY) ? 0 : (stt_size() - 1);
         index != -1 && visible_row_count < STT_INFO_MAX_MSG_NUM;
         index--) {
        const char* text = stt_buffer_get_transcribe_by_index(index);
        lv_base_dir_t base_dir =
            stt_config.sourceTextDirection == TEXT_DIRECTION_RTL ? LV_BASE_DIR_RTL : LV_BASE_DIR_LTR;
        bool is_bottom_most = false;
        bool is_final = false;

        if (text == NULL || text[0] == '\0') {
            continue;
        }

        is_bottom_most = (stt_config.textMode != TEXTMODE_HISTORY) || (index == 0);
        is_final = stt_buffer_get_is_final_by_index(index);
        transcribe_stt_update_row(&transcribe_stt_rows[visible_row_count],
                                  text,
                                  base_dir,
                                  is_bottom_most,
                                  is_final);
        ++visible_row_count;
        if (stt_config.textMode != TEXTMODE_HISTORY) {
            break;
        }
    }

    for (size_t i = visible_row_count; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (transcribe_stt_rows[i].row != NULL) {
            ui_widget_set_visible(UI_WIDGET(transcribe_stt_rows[i].row), false);
        }
    }

    container_scroll_to_bottom(transcribe_scroll, LV_ANIM_OFF);
    update_end_us = (uint32_t)GetTimeUs();
    floatair_info("transcribe refresh timing: total=%luus/%lums rows=%lu",
                  (unsigned long)(update_end_us - update_start_us),
                  (unsigned long)((update_end_us - update_start_us) / 1000U),
                  (unsigned long)visible_row_count);
}

/**
 * @brief 清空转写页 STT 内容。
 *
 * @param 无参数。
 * @return 无返回值。
 */
void transcribe_stt_clear(void) {
    stt_buffer_init();
    if (transcribe_scroll != NULL) {
        transcribe_stt_hide_all_rows();
        ui_widget_set_visible(UI_WIDGET(transcribe_scroll), false);
    }
}

/**
 * @brief 页面触控事件处理。
 *
 * @param event LVGL 事件对象。
 * @return 无返回值。
 */
static void touch_event_handle(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    bool can_scroll = (transcribe_mode == VIEW_MODE_FUNC_STT &&
                       transcribe_scroll != NULL &&
                       stt_size() > 0 &&
                       stt_buffer_get_is_final_by_index(0));

    switch (code) {
    case LV_EVENT_GESTURE_LEFT:
        if (transcribe_mode == VIEW_MODE_FUNC_STT) {
            if (transcribe_scroll == NULL) {
                floatair_info("transcribe_scroll == NULL");
                break;
            }
            if (can_scroll) {
                container_scroll_up(transcribe_scroll, 3.0f / 4.0f);
            }
        }
        break;
    case LV_EVENT_GESTURE_RIGHT:
        if (transcribe_mode == VIEW_MODE_FUNC_STT) {
            if (transcribe_scroll == NULL) {
                floatair_info("transcribe_scroll == NULL");
                break;
            }
            if (can_scroll) {
                container_scroll_down(transcribe_scroll, 3.0f / 4.0f);
            }
        }
        break;
    case LV_EVENT_CLICKED:
    case LV_EVENT_LONG_PRESSED:
        if (transcribe_mode == VIEW_MODE_FUNC_DESCRIPTION) {
            transcribe_mode_go_wait();
        } else {
            system_report_touch_event(code);
        }
        break;
    case LV_EVENT_DCLICKED:
        (void)app_router_exit_current_app();
        break;
    default:
        break;
    }
}

/**
 * @brief 字体配置变化后刷新页面布局。
 *
 * @param 无参数。
 * @return 无返回值。
 */
void transcribe_on_fontconfig_changed(void) {
    lv_obj_t* lang_obj = NULL;
    lv_obj_t* init_label_obj = NULL;
    lv_obj_t* notice_obj = NULL;

    stt_style_init();

    stt_view_apply_text_theme(transcribe_init_label, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    stt_view_apply_text_theme(transcribe_notice_op, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    init_label_obj = label_get_obj(transcribe_init_label);
    if (init_label_obj != NULL) {
        lv_obj_set_width(init_label_obj, LV_PCT(100));
        lv_obj_align_to(init_label_obj,
                        ui_widget_get_obj(UI_WIDGET(transcribe_init_img)),
                        LV_ALIGN_OUT_BOTTOM_MID,
                        0,
                        10);
    }
    notice_obj = label_get_obj(transcribe_notice_op);
    if (notice_obj != NULL) {
        lv_obj_set_size(notice_obj, LV_PCT(100), stt_get_font_height());
        lv_obj_align(notice_obj, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    if (transcribe_lang != NULL) {
        lang_obj = label_get_obj(transcribe_lang);
        if (lang_obj != NULL) {
            lv_obj_set_size(lang_obj, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_min_height(lang_obj, STATUS_BAR_IMG_H, 0);
        }
    }
    if (transcribe_content != NULL) {
        lv_obj_move_foreground(container_get_obj(transcribe_content));
    }
    if (notice_obj != NULL) {
        lv_obj_move_foreground(notice_obj);
    }
    transcribe_update_lang_hint();
    transcribe_stt_reset_row_labels();
    if (transcribe_mode == VIEW_MODE_FUNC_STT) {
        transcribe_stt_update();
    }
}

/**
 * @brief 刷新语言提示区。
 *
 * @param 无参数。
 * @return 无返回值。
 */
void transcribe_update_lang_hint(void) {
    uint32_t set_text_start_us = 0;
    uint32_t set_text_cost_us = 0;
    uint32_t layout_start_us = 0;
    uint32_t layout_cost_us = 0;
    lv_base_dir_t base_dir = LV_BASE_DIR_LTR;

    if (transcribe_lang == NULL) {
        floatair_err("transcribe_lang is NULL");
        return;
    }

    if (stt_config.language_hint == 0 && stt_config.language_source[0] != '\0') {
        base_dir = stt_config.sourceTextDirection == TEXT_DIRECTION_RTL ? LV_BASE_DIR_RTL : LV_BASE_DIR_LTR;
        set_text_start_us = (uint32_t)GetTimeUs();
        label_set_text(transcribe_lang, stt_config.language_source);
        set_text_cost_us = (uint32_t)GetTimeUs() - set_text_start_us;
        stt_view_apply_lang_hint_theme(transcribe_lang, TRANSCRIBE_LANG_HINT_PADDING, base_dir);
        ui_widget_set_visible(UI_WIDGET(transcribe_lang), true);
        if (transcribe_content != NULL) {
            layout_start_us = (uint32_t)GetTimeUs();
            lv_obj_update_layout(container_get_obj(transcribe_content));
            layout_cost_us = (uint32_t)GetTimeUs() - layout_start_us;
        }
        floatair_info("transcribe lang hint timing: set_text=%luus/%lums layout=%luus/%lums visible=1",
                      (unsigned long)set_text_cost_us,
                      (unsigned long)(set_text_cost_us / 1000U),
                      (unsigned long)layout_cost_us,
                      (unsigned long)(layout_cost_us / 1000U));
        return;
    }

    set_text_start_us = (uint32_t)GetTimeUs();
    label_set_text(transcribe_lang, "");
    set_text_cost_us = (uint32_t)GetTimeUs() - set_text_start_us;
    ui_widget_set_visible(UI_WIDGET(transcribe_lang), false);
    if (transcribe_content != NULL) {
        layout_start_us = (uint32_t)GetTimeUs();
        lv_obj_update_layout(container_get_obj(transcribe_content));
        layout_cost_us = (uint32_t)GetTimeUs() - layout_start_us;
    }
    floatair_info("transcribe lang hint timing: set_text=%luus/%lums layout=%luus/%lums visible=0",
                  (unsigned long)set_text_cost_us,
                  (unsigned long)(set_text_cost_us / 1000U),
                  (unsigned long)layout_cost_us,
                  (unsigned long)(layout_cost_us / 1000U));
}

/**
 * @brief 页面加载回调。
 *
 * @param[in] root 页面根对象。
 * @param[in] data 页面入参。
 * @return 无返回值。
 */
static void transcribe_page_create(lv_obj_t* root, const app_page_data_t* data) {
    lv_obj_t* status_bar = NULL;
    lv_obj_t* content_obj = NULL;
    lv_obj_t* lang_obj = NULL;
    lv_obj_t* scroll_obj = NULL;

    (void)data;
    floatair_info("entry");
    floatair_assert(root != NULL, "root NULL");
    transcribe_root = root;

    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }

    stt_style_init();

    transcribe_init_img = stt_view_create_center_image(root);
    floatair_assert(transcribe_init_img != NULL, "transcribe_init_img NULL");

    transcribe_init_label = stt_view_create_text_label(root,
                                                       LV_PCT(100),
                                                       LV_SIZE_CONTENT,
                                                       "",
                                                       LABEL_ALIGN_CENTER,
                                                       LABEL_OVERFLOW_WRAP);
    floatair_assert(transcribe_init_label != NULL, "transcribe_init_label NULL");
    stt_view_apply_text_theme(transcribe_init_label, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    lv_obj_align_to(label_get_obj(transcribe_init_label),
                    ui_widget_get_obj(UI_WIDGET(transcribe_init_img)),
                    LV_ALIGN_OUT_BOTTOM_MID,
                    0,
                    10);

    transcribe_content = stt_view_create_plain_container(root, LV_PCT(100), LV_PCT(100));
    floatair_assert(transcribe_content != NULL, "transcribe_content NULL");
    container_set_layout_vbox(transcribe_content);
    container_set_align(transcribe_content,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START);
    container_set_padding_box(transcribe_content, 0, 0, TRANSCRIBE_SCROLL_TOP_MARGIN, 0);
    content_obj = container_get_obj(transcribe_content);

    transcribe_lang = stt_view_create_text_label(content_obj,
                                                 LV_PCT(100),
                                                 LV_SIZE_CONTENT,
                                                 "",
                                                 LABEL_ALIGN_CENTER,
                                                 LABEL_OVERFLOW_SCROLL_CIRCULAR);
    floatair_assert(transcribe_lang != NULL, "transcribe_lang NULL");
    stt_view_apply_lang_hint_text_theme(transcribe_lang,
                                        TRANSCRIBE_LANG_HINT_PADDING,
                                        stt_config.sourceTextDirection == TEXT_DIRECTION_RTL
                                            ? LV_BASE_DIR_RTL
                                            : LV_BASE_DIR_LTR);
    lang_obj = label_get_obj(transcribe_lang);
    lv_obj_set_style_min_height(lang_obj, STATUS_BAR_IMG_H, 0);

    transcribe_notice_op = stt_view_create_text_label(root,
                                                      LV_PCT(100),
                                                      stt_get_font_height(),
                                                      "",
                                                      LABEL_ALIGN_CENTER,
                                                      LABEL_OVERFLOW_WRAP);
    floatair_assert(transcribe_notice_op != NULL, "transcribe_notice_op NULL");
    stt_view_apply_text_theme(transcribe_notice_op, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    lv_obj_align(label_get_obj(transcribe_notice_op), LV_ALIGN_BOTTOM_MID, 0, 0);

    floatair_assert(transcribe_status_bar_ensure_widgets(), "transcribe status bar widgets are NULL");

    transcribe_scroll = stt_view_create_plain_container(content_obj, LV_PCT(100), 0);
    floatair_assert(transcribe_scroll != NULL, "transcribe_scroll NULL");
    scroll_obj = container_get_obj(transcribe_scroll);
    container_set_child_grow(scroll_obj, 1);
    container_set_layout_vbox_spaced(transcribe_scroll, get_system_font_row_space());
    container_set_align(transcribe_scroll,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START);
    container_set_padding_box(transcribe_scroll,
                              TRANSCRIBE_TEXT_SIDE_PADDING,
                              TRANSCRIBE_TEXT_SIDE_PADDING,
                              0,
                              0);
    container_set_scrollable(transcribe_scroll, true);
    container_set_scroll_dir(transcribe_scroll, LV_DIR_VER);
    container_set_scrollbar_mode(transcribe_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_move_foreground(content_obj);
    lv_obj_move_foreground(label_get_obj(transcribe_notice_op));
    transcribe_stt_init_rows();

    if (app_router_get_entry_mode() == APP_ROUTER_ENTRY_LOCAL) {
        transcribe_mode_go_description();
    } else if (app_router_get_entry_mode() == APP_ROUTER_ENTRY_REMOTE) {
        transcribe_mode_go_wait();
    } else {
        transcribe_mode_go_none();
    }

    floatair_info("Done");
}

static void transcribe_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    (void)system_request_keyword_spotting_enabled(false);
    system_status_bar_set_mode(true);
    floatair_assert(transcribe_status_bar_ensure_widgets(), "transcribe status bar widgets are NULL");
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
    transcribe_stt_update();
}

static void transcribe_page_destroy(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    (void)system_request_keyword_spotting_enabled(system_config_get_keyword_spotting_enabled());

    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }

    transcribe_root = NULL;
    transcribe_init_label = NULL;
    transcribe_init_img = NULL;
    transcribe_notice_op = NULL;
    transcribe_audio_source = NULL;
    transcribe_mic_direction = NULL;
    transcribe_waveicon = NULL;
    transcribe_content = NULL;
    transcribe_lang = NULL;
    transcribe_scroll = NULL;
    transcribe_scroll_spacer = NULL;
    memset(transcribe_stt_rows, 0, sizeof(transcribe_stt_rows));
    transcribe_mode = VIEW_MODE_FUNC_NONE;
}

static app_page_t s_transcribe_page = {
    .name = APP_NAME_TRANSCRIBE,
    .on_create = transcribe_page_create,
    .on_appear = transcribe_page_appear,
    .on_disappear = NULL,
    .on_destroy = transcribe_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* transcribe_page_get(void) {
    return &s_transcribe_page;
}
