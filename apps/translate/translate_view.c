/**
 * @file translate_view.c
 * @brief 翻译应用页面视图实现
 */
#include "home/home.h"
#include "stt_view_common.h"
#include "translate.h"

#include <string.h>

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

/**
 * @brief 翻译页面当前展示模式。
 */
typedef enum {
    VIEW_MODE_FUNC_NONE = 0,        ///< 页面内容全部隐藏。
    VIEW_MODE_FUNC_DESCRIPTION = 1, ///< 展示功能说明态。
    VIEW_MODE_FUNC_WAIT = 2,        ///< 展示等待连接态。
    VIEW_MODE_FUNC_STT = 3,         ///< 展示实时转写/翻译态。
} view_mode_t;

#define TRANSLATE_TEXT_SIDE_PADDING 12
#define TRANSLATE_SCROLL_TOP_MARGIN 2
#define TRANSLATE_LANG_HINT_GAP LVGL_UI_MARGIN_10
/**
 * @brief 翻译页单条 STT 行视图缓存。
 */
typedef struct {
    container_t* row;       ///< 行根容器。
    label_t* first_label;   ///< 第一段文本标签。
    label_t* second_label;  ///< 第二段文本标签。
} translate_stt_row_t;

static label_t* translate_init_label = NULL;
static img_t* translate_init_img = NULL;
static label_t* translate_notice_op = NULL;
static lv_obj_t* translate_audio_source = NULL;
static lv_obj_t* translate_mic_direction = NULL;
static lv_obj_t* translate_waveicon = NULL;
static lv_obj_t* translate_root = NULL;
static container_t* translate_content = NULL;
static container_t* translate_lang = NULL;
static container_t* translate_lang_row = NULL;
static label_t* translate_lable_lang_source = NULL;
static label_t* translate_lable_lang_target = NULL;
static img_t* translate_img_lang_hint = NULL;
static container_t* translate_scroll = NULL;
static container_t* translate_scroll_spacer = NULL;
static translate_stt_row_t translate_stt_rows[STT_INFO_MAX_MSG_NUM];
static view_mode_t translate_mode = VIEW_MODE_FUNC_NONE;

/**
 * @brief 判断翻译页底栏自定义图标句柄是否仍然有效。
 * @return `true` 表示三个图标都有效，`false` 表示需要重建。
 */
static bool translate_status_bar_widgets_valid(void) {
    return translate_audio_source != NULL &&
           lv_obj_is_valid(translate_audio_source) &&
           translate_waveicon != NULL &&
           lv_obj_is_valid(translate_waveicon) &&
           translate_mic_direction != NULL &&
           lv_obj_is_valid(translate_mic_direction);
}

/**
 * @brief 确保翻译页底栏自定义图标存在且可用。
 * @return `true` 表示图标已就绪，`false` 表示状态栏不可用。
 */
static bool translate_status_bar_ensure_widgets(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        translate_audio_source = NULL;
        translate_waveicon = NULL;
        translate_mic_direction = NULL;
        return false;
    }
    if (!translate_status_bar_widgets_valid()) {
        status_bar_clear_custom_widgets(status_bar);
        translate_audio_source = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("sound_phone.jpg"), STATUS_BAR_WIDGET_ALIGN_LEFT);
        translate_waveicon = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("sound_wave.jpg"), STATUS_BAR_WIDGET_ALIGN_RIGHT);
        translate_mic_direction = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("micphone.jpg"), STATUS_BAR_WIDGET_ALIGN_RIGHT);
    }

    return translate_status_bar_widgets_valid();
}

/**
 * @brief 按当前页面内容区尺寸重新同步翻译页关键控件布局。
 * @return 无返回值。
 */
static void translate_view_sync_layout(void) {
    lv_obj_t* root = translate_root;

    if (root == NULL || !lv_obj_is_valid(root)) {
        return;
    }

    if (translate_init_img != NULL) {
        lv_obj_align(ui_widget_get_obj(UI_WIDGET(translate_init_img)), LV_ALIGN_CENTER, 0, 0);
    }
    if (translate_init_label != NULL && translate_init_img != NULL) {
        lv_obj_set_width(label_get_obj(translate_init_label), (lv_coord_t)config_lcd.ui_width);
        lv_obj_align_to(label_get_obj(translate_init_label),
                        ui_widget_get_obj(UI_WIDGET(translate_init_img)),
                        LV_ALIGN_OUT_BOTTOM_MID,
                        0,
                        10);
    }
    if (translate_notice_op != NULL) {
        lv_obj_set_size(label_get_obj(translate_notice_op),
                        (lv_coord_t)config_lcd.ui_width,
                        stt_get_font_height());
        lv_obj_align(label_get_obj(translate_notice_op), LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    if (translate_lang != NULL) {
        lv_obj_set_width(container_get_obj(translate_lang), (lv_coord_t)config_lcd.ui_width);
    }
    if (translate_scroll != NULL) {
        lv_obj_set_width(container_get_obj(translate_scroll), (lv_coord_t)config_lcd.ui_width);
    }
}

/**
 * @brief 切换到空白模式。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void translate_mode_go_none(void) {
    translate_mode = VIEW_MODE_FUNC_NONE;
    ui_widget_set_visible(UI_WIDGET(translate_init_label), false);
    ui_widget_set_visible(UI_WIDGET(translate_init_img), false);
    ui_widget_set_visible(UI_WIDGET(translate_notice_op), false);
    if (translate_audio_source != NULL && lv_obj_is_valid(translate_audio_source)) {
        lv_obj_add_flag(translate_audio_source, LV_OBJ_FLAG_HIDDEN);
    }
    if (translate_mic_direction != NULL && lv_obj_is_valid(translate_mic_direction)) {
        lv_obj_add_flag(translate_mic_direction, LV_OBJ_FLAG_HIDDEN);
    }
    ui_widget_set_visible(UI_WIDGET(translate_content), false);
    ui_widget_set_visible(UI_WIDGET(translate_lang), false);
    ui_widget_set_visible(UI_WIDGET(translate_scroll), false);
    stt_view_update_waveicon(translate_waveicon, false);
}

/**
 * @brief 切换到功能说明模式。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void translate_mode_go_description(void) {
    translate_mode = VIEW_MODE_FUNC_DESCRIPTION;
    ui_widget_set_visible(UI_WIDGET(translate_init_label), true);
    if (translate_init_label != NULL) {
        lv_label_set_text_fmt(label_get_obj(translate_init_label),
                              "%s%s",
                              app_get_str("TRANS_GUIDE"),
                              app_get_str("SYSTEM_APP"));
    }
    ui_widget_set_visible(UI_WIDGET(translate_init_img), false);
    ui_widget_set_visible(UI_WIDGET(translate_notice_op), true);
    if (translate_notice_op != NULL) {
        lv_label_set_text_fmt(label_get_obj(translate_notice_op),
                              "%s%s%s",
                              app_get_str("SYSTEM_LP_TOUCH"),
                              app_get_str("SYSTEM_LP_OPEN"),
                              app_get_str("TRANS_TEXT"));
        lv_obj_update_layout(label_get_obj(translate_notice_op));
    }
    if (translate_audio_source != NULL && lv_obj_is_valid(translate_audio_source)) {
        lv_obj_add_flag(translate_audio_source, LV_OBJ_FLAG_HIDDEN);
    }
    if (translate_mic_direction != NULL && lv_obj_is_valid(translate_mic_direction)) {
        lv_obj_add_flag(translate_mic_direction, LV_OBJ_FLAG_HIDDEN);
    }
    ui_widget_set_visible(UI_WIDGET(translate_content), false);
    ui_widget_set_visible(UI_WIDGET(translate_lang), false);
    ui_widget_set_visible(UI_WIDGET(translate_scroll), false);
    stt_view_update_waveicon(translate_waveicon, false);
}

/**
 * @brief 切换到等待连接模式。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void translate_mode_go_wait(void) {
    translate_mode = VIEW_MODE_FUNC_WAIT;
    ui_widget_set_visible(UI_WIDGET(translate_init_label), true);
    if (translate_init_label != NULL) {
        lv_label_set_text_fmt(label_get_obj(translate_init_label),
                              "%s%s",
                              app_get_str("TRANS_TEXT"),
                              app_get_str("SYSTEM_OPENIGN"));
    }
    ui_widget_set_visible(UI_WIDGET(translate_init_img), true);
    if (translate_init_img != NULL) {
        img_set_src(translate_init_img, FLOATAIR_SYS_IMG("connecting.jpg"));
    }
    ui_widget_set_visible(UI_WIDGET(translate_notice_op), false);
    stt_view_update_audio_source(translate_audio_source);
    stt_view_update_mic_direction(translate_mic_direction);
    ui_widget_set_visible(UI_WIDGET(translate_content), false);
    ui_widget_set_visible(UI_WIDGET(translate_lang), false);
    ui_widget_set_visible(UI_WIDGET(translate_scroll), false);
    stt_view_update_waveicon(translate_waveicon, false);
}

/**
 * @brief 切换到 STT 展示模式。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void translate_mode_go_stt(void) {
    translate_mode = VIEW_MODE_FUNC_STT;
    ui_widget_set_visible(UI_WIDGET(translate_init_label), false);
    ui_widget_set_visible(UI_WIDGET(translate_init_img), false);
    ui_widget_set_visible(UI_WIDGET(translate_notice_op), false);
    stt_view_update_audio_source(translate_audio_source);
    stt_view_update_mic_direction(translate_mic_direction);
    ui_widget_set_visible(UI_WIDGET(translate_content), true);
    translate_update_lang_hint();
    ui_widget_set_visible(UI_WIDGET(translate_scroll), true);
    if (translate_content != NULL) {
        lv_obj_update_layout(container_get_obj(translate_content));
    }
    stt_view_update_waveicon(translate_waveicon, true);
}

/**
 * @brief 初始化单条 STT 行缓存。
 *
 * @param row 目标行缓存。
 * @param parent 父对象。
 * @return 无返回值。
 */
static void translate_stt_init_row(translate_stt_row_t* row, lv_obj_t* parent) {
    if (row == NULL || parent == NULL) {
        return;
    }
    if (row->row != NULL) {
        return;
    }

    row->row = stt_view_create_plain_container(parent, LV_PCT(100), LV_SIZE_CONTENT);
    floatair_assert(row->row != NULL, "translate stt row NULL");
    container_set_layout_vbox(row->row);
    container_set_align(row->row, CONTAINER_ALIGN_START, CONTAINER_ALIGN_START, CONTAINER_ALIGN_START);
    row->first_label = NULL;
    row->second_label = NULL;
    ui_widget_set_visible(UI_WIDGET(row->row), false);
}

/**
 * @brief 清空翻译页 STT 行内已创建的标签缓存。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void translate_stt_reset_row_labels(void) {
    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        lv_obj_t* row_obj = NULL;

        if (translate_stt_rows[i].row == NULL) {
            continue;
        }

        row_obj = container_get_obj(translate_stt_rows[i].row);
        if (row_obj != NULL) {
            lv_obj_clean(row_obj);
        }
        translate_stt_rows[i].first_label = NULL;
        translate_stt_rows[i].second_label = NULL;
    }
}

/**
 * @brief 按当前数据刷新单条 STT 行内容。
 *
 * @param row 目标行缓存。
 * @param first 第一段文本。
 * @param first_dir 第一段文本方向。
 * @param second 第二段文本。
 * @param second_dir 第二段文本方向。
 * @param is_bottom_most 是否为当前高亮内容。
 * @return 无返回值。
 */
static void translate_stt_update_row(translate_stt_row_t* row,
                                     const char* first,
                                     lv_base_dir_t first_dir,
                                     const char* second,
                                     lv_base_dir_t second_dir,
                                     bool is_bottom_most,
                                     bool is_final) {
    uint32_t first_set_text_start_us = 0;
    uint32_t first_set_text_cost_us = 0;
    uint32_t second_set_text_start_us = 0;
    uint32_t second_set_text_cost_us = 0;
    uint32_t layout_start_us = 0;
    uint32_t layout_cost_us = 0;
    lv_obj_t* row_obj = NULL;
    bool has_first = (first != NULL && first[0] != '\0');
    bool has_second = (second != NULL && second[0] != '\0');
    LV_UNUSED(is_final);

    if (row == NULL || row->row == NULL || (!has_first && !has_second)) {
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

    container_set_spacing(row->row, has_first && has_second ? get_system_font_row_space() : 0);
    ui_widget_set_size(UI_WIDGET(row->row), LV_PCT(100), LV_SIZE_CONTENT);
    ui_widget_set_visible(UI_WIDGET(row->row), true);

    if (has_first) {
        if (row->first_label == NULL) {
            row->first_label = stt_view_create_text_label(row_obj,
                                                          LV_PCT(100),
                                                          LV_SIZE_CONTENT,
                                                          "",
                                                          LABEL_ALIGN_LEFT,
                                                          LABEL_OVERFLOW_WRAP);
            floatair_assert(row->first_label != NULL, "translate stt first_label NULL");
            stt_view_apply_stt_label_text_theme(row->first_label, first_dir);
        }
        first_set_text_start_us = (uint32_t)GetTimeUs();
        stt_view_update_incremental_text(row->first_label, first);
        first_set_text_cost_us = (uint32_t)GetTimeUs() - first_set_text_start_us;
        ui_widget_set_size(UI_WIDGET(row->first_label), LV_PCT(100), LV_SIZE_CONTENT);
        ui_widget_set_visible(UI_WIDGET(row->first_label), true);
        stt_view_apply_stt_label_theme(row->first_label, first_dir, is_bottom_most);
    } else if (row->first_label != NULL) {
        label_set_text(row->first_label, "");
        ui_widget_set_visible(UI_WIDGET(row->first_label), false);
    }
    if (has_second) {
        if (row->second_label == NULL) {
            row->second_label = stt_view_create_text_label(row_obj,
                                                           LV_PCT(100),
                                                           LV_SIZE_CONTENT,
                                                           "",
                                                           LABEL_ALIGN_LEFT,
                                                           LABEL_OVERFLOW_WRAP);
            floatair_assert(row->second_label != NULL, "translate stt second_label NULL");
            stt_view_apply_stt_label_text_theme(row->second_label, second_dir);
        }
        second_set_text_start_us = (uint32_t)GetTimeUs();
        stt_view_update_incremental_text(row->second_label, second);
        second_set_text_cost_us = (uint32_t)GetTimeUs() - second_set_text_start_us;
        ui_widget_set_size(UI_WIDGET(row->second_label), LV_PCT(100), LV_SIZE_CONTENT);
        ui_widget_set_visible(UI_WIDGET(row->second_label), true);
        stt_view_apply_stt_label_theme(row->second_label, second_dir, is_bottom_most);
    } else if (row->second_label != NULL) {
        label_set_text(row->second_label, "");
        ui_widget_set_visible(UI_WIDGET(row->second_label), false);
    }

    layout_start_us = (uint32_t)GetTimeUs();
    lv_obj_update_layout(row_obj);
    layout_cost_us = (uint32_t)GetTimeUs() - layout_start_us;
    floatair_info("translate row timing: first_set=%luus/%lums second_set=%luus/%lums layout=%luus/%lums final=%d bottom=%d",
                  (unsigned long)first_set_text_cost_us,
                  (unsigned long)(first_set_text_cost_us / 1000U),
                  (unsigned long)second_set_text_cost_us,
                  (unsigned long)(second_set_text_cost_us / 1000U),
                  (unsigned long)layout_cost_us,
                  (unsigned long)(layout_cost_us / 1000U),
                  is_final ? 1 : 0,
                  is_bottom_most ? 1 : 0);
}

/**
 * @brief 创建翻译页 STT 行缓存。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void translate_stt_init_rows(void) {
    lv_obj_t* scroll_obj = NULL;

    if (translate_scroll == NULL) {
        return;
    }

    scroll_obj = container_get_obj(translate_scroll);
    if (scroll_obj == NULL) {
        return;
    }

    if (translate_scroll_spacer == NULL) {
        translate_scroll_spacer = stt_view_create_plain_container(scroll_obj, LV_PCT(100), 0);
        floatair_assert(translate_scroll_spacer != NULL, "translate_scroll_spacer NULL");
        container_set_child_grow(container_get_obj(translate_scroll_spacer), 1);
    }

    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        translate_stt_init_row(&translate_stt_rows[i], scroll_obj);
    }
}

/**
 * @brief 隐藏所有 STT 行缓存。
 *
 * @param 无参数。
 * @return 无返回值。
 */
static void translate_stt_hide_all_rows(void) {
    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (translate_stt_rows[i].row != NULL) {
            ui_widget_set_visible(UI_WIDGET(translate_stt_rows[i].row), false);
        }
    }
}

/**
 * @brief 刷新翻译 STT 内容区域。
 *
 * @param 无参数。
 * @return 无返回值。
 */
void translate_stt_update(void) {
    uint32_t update_start_us = (uint32_t)GetTimeUs();
    uint32_t update_end_us = 0;
    const bool stt_trans_first = false;
    size_t visible_row_count = 0;
    lv_obj_t* scroll_obj = NULL;

    translate_update_lang_hint();

    if (translate_scroll == NULL) {
        floatair_info("translate_scroll == NULL");
        return;
    }

    if (container_get_obj(translate_scroll) == NULL) {
        floatair_info("translate_scroll obj == NULL");
        return;
    }
    scroll_obj = container_get_obj(translate_scroll);

    if (stt_size() == 0) {
        floatair_info("stt_size() == 0");
        translate_stt_hide_all_rows();
        ui_widget_set_visible(UI_WIDGET(translate_scroll), false);
        if (translate_mode == VIEW_MODE_FUNC_WAIT || translate_mode == VIEW_MODE_FUNC_STT) {
            stt_view_update_audio_source(translate_audio_source);
            stt_view_update_mic_direction(translate_mic_direction);
        }
        return;
    }
    if (translate_mode != VIEW_MODE_FUNC_STT) {
        translate_mode_go_stt();
    } else {
        stt_view_update_audio_source(translate_audio_source);
        stt_view_update_mic_direction(translate_mic_direction);
    }

    translate_stt_init_rows();

    for (int index = (stt_config.textMode != TEXTMODE_HISTORY) ? 0 : (stt_size() - 1);
         index != -1 && visible_row_count < STT_INFO_MAX_MSG_NUM;
         index--) {
        const char* t1 = stt_buffer_get_transcribe_by_index(index);
        const char* t2 = stt_buffer_get_translate_by_index(index);
        const char* first = NULL;
        const char* second = NULL;
        lv_base_dir_t first_dir = LV_BASE_DIR_LTR;
        lv_base_dir_t second_dir = LV_BASE_DIR_LTR;
        bool has_first = false;
        bool has_second = false;
        bool is_bottom_most = false;
        bool is_final = false;
        uint8_t src_dir = stt_config.sourceTextDirection;
        uint8_t dst_dir = stt_config.targetTextDirection;

        if (stt_config.transMode == TRANSMODE_SHOW_ONLY_TRANS) {
            first = t2;
        } else if (stt_config.transMode == TRANSMODE_SHOW_ONLY_ORI) {
            first = t1;
        } else {
            first = stt_trans_first ? t2 : t1;
            second = stt_trans_first ? t1 : t2;
        }

        has_first = (first != NULL && first[0] != '\0');
        has_second = (second != NULL && second[0] != '\0');
        if (!has_first && !has_second) {
            continue;
        }

        is_bottom_most = (stt_config.textMode != TEXTMODE_HISTORY) || (index == 0);
        is_final = stt_buffer_get_is_final_by_index(index);
        if (has_first) {
            bool is_source = (first == t1);

            first_dir = is_source && src_dir == TEXT_DIRECTION_RTL
                            ? LV_BASE_DIR_RTL
                            : ((!is_source && dst_dir == TEXT_DIRECTION_RTL)
                                   ? LV_BASE_DIR_RTL
                                   : LV_BASE_DIR_LTR);
        }
        if (has_second) {
            bool is_source = (second == t1);

            second_dir = is_source && src_dir == TEXT_DIRECTION_RTL
                             ? LV_BASE_DIR_RTL
                             : ((!is_source && dst_dir == TEXT_DIRECTION_RTL)
                                    ? LV_BASE_DIR_RTL
                                    : LV_BASE_DIR_LTR);
        }

        translate_stt_update_row(&translate_stt_rows[visible_row_count],
                                 first,
                                 first_dir,
                                 second,
                                 second_dir,
                                 is_bottom_most,
                                 is_final);
        ++visible_row_count;
        if (stt_config.textMode != TEXTMODE_HISTORY) {
            break;
        }
    }

    for (size_t i = visible_row_count; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (translate_stt_rows[i].row != NULL) {
            ui_widget_set_visible(UI_WIDGET(translate_stt_rows[i].row), false);
        }
    }

    container_scroll_to_bottom(translate_scroll, LV_ANIM_OFF);
    if (visible_row_count > 0 && translate_stt_rows[visible_row_count - 1].row != NULL) {
        lv_obj_scroll_to_view(container_get_obj(translate_stt_rows[visible_row_count - 1].row), LV_ANIM_OFF);
    }
    if (scroll_obj != NULL) {
        lv_obj_invalidate(scroll_obj);
    }
    update_end_us = (uint32_t)GetTimeUs();
    floatair_info("translate refresh timing: total=%luus/%lums rows=%lu",
                  (unsigned long)(update_end_us - update_start_us),
                  (unsigned long)((update_end_us - update_start_us) / 1000U),
                  (unsigned long)visible_row_count);
}

/**
 * @brief 清空翻译页 STT 内容。
 *
 * @param 无参数。
 * @return 无返回值。
 */
void translate_stt_clear(void) {
    stt_buffer_init();
    if (translate_scroll != NULL) {
        translate_stt_hide_all_rows();
        ui_widget_set_visible(UI_WIDGET(translate_scroll), false);
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
    bool can_scroll = (translate_mode == VIEW_MODE_FUNC_STT &&
                       translate_scroll != NULL &&
                       stt_size() > 0 &&
                       stt_buffer_get_is_final_by_index(0));
    switch (code) {
    case LV_EVENT_GESTURE_LEFT:
        if (translate_mode == VIEW_MODE_FUNC_STT) {
            if (translate_scroll == NULL) {
                floatair_info("translate_scroll == NULL");
                break;
            }
            if (can_scroll) {
                container_scroll_up(translate_scroll, 3.0f / 4.0f);
            }
        }
        break;
    case LV_EVENT_GESTURE_RIGHT:
        if (translate_mode == VIEW_MODE_FUNC_STT) {
            if (translate_scroll == NULL) {
                floatair_info("translate_scroll == NULL");
                break;
            }
            if (can_scroll) {
                container_scroll_down(translate_scroll, 3.0f / 4.0f);
            }
        }
        break;
    case LV_EVENT_CLICKED:
    case LV_EVENT_LONG_PRESSED:
        if (translate_mode == VIEW_MODE_FUNC_DESCRIPTION) {
            translate_mode_go_wait();
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
void translate_on_fontconfig_changed(void) {
    lv_obj_t* lang_obj = NULL;
    lv_obj_t* init_label_obj = NULL;
    lv_obj_t* notice_obj = NULL;

    stt_style_init();

    stt_view_apply_text_theme(translate_init_label, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    stt_view_apply_text_theme(translate_notice_op, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    init_label_obj = label_get_obj(translate_init_label);
    if (init_label_obj != NULL) {
        lv_obj_set_width(init_label_obj, LV_PCT(100));
        lv_obj_align_to(init_label_obj,
                        ui_widget_get_obj(UI_WIDGET(translate_init_img)),
                        LV_ALIGN_OUT_BOTTOM_MID,
                        0,
                        10);
    }
    notice_obj = label_get_obj(translate_notice_op);
    if (notice_obj != NULL) {
        lv_obj_set_size(notice_obj, LV_PCT(100), stt_get_font_height());
        lv_obj_align(notice_obj, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    if (translate_lang != NULL) {
        lang_obj = container_get_obj(translate_lang);
        if (lang_obj != NULL) {
            lv_obj_set_size(lang_obj, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_min_height(lang_obj, STATUS_BAR_IMG_H, 0);
            container_set_padding(translate_lang, 0, 3);
        }
    }
    if (translate_lang_row != NULL) {
        lv_obj_set_size(container_get_obj(translate_lang_row), LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    }
    if (translate_lable_lang_source != NULL) {
        stt_view_apply_lang_hint_text_theme(translate_lable_lang_source,
                                            TRANSLATE_LANG_HINT_GAP,
                                            stt_config.sourceTextDirection == TEXT_DIRECTION_RTL
                                                ? LV_BASE_DIR_RTL
                                                : LV_BASE_DIR_LTR);
    }
    if (translate_lable_lang_target != NULL) {
        stt_view_apply_lang_hint_text_theme(translate_lable_lang_target,
                                            TRANSLATE_LANG_HINT_GAP,
                                            stt_config.targetTextDirection == TEXT_DIRECTION_RTL
                                                ? LV_BASE_DIR_RTL
                                                : LV_BASE_DIR_LTR);
    }
    if (translate_content != NULL) {
        lv_obj_move_foreground(container_get_obj(translate_content));
    }
    if (notice_obj != NULL) {
        lv_obj_move_foreground(notice_obj);
    }
    translate_update_lang_hint();
    translate_stt_reset_row_labels();
    if (translate_mode == VIEW_MODE_FUNC_STT) {
        translate_stt_update();
    }
}

/**
 * @brief 刷新语言提示区。
 *
 * @param 无参数。
 * @return 无返回值。
 */
void translate_update_lang_hint(void) {
    uint32_t src_set_text_start_us = 0;
    uint32_t src_set_text_cost_us = 0;
    uint32_t dst_set_text_start_us = 0;
    uint32_t dst_set_text_cost_us = 0;
    uint32_t lang_layout_start_us = 0;
    uint32_t lang_layout_cost_us = 0;
    uint32_t content_layout_start_us = 0;
    uint32_t content_layout_cost_us = 0;
    lv_obj_t* lang_obj = NULL;
    lv_obj_t* lang_row_obj = NULL;

    if (translate_lang == NULL) {
        floatair_err("translate_lang is NULL");
        return;
    }

    lang_obj = container_get_obj(translate_lang);
    if (lang_obj == NULL) {
        floatair_err("translate_lang obj is NULL");
        return;
    }

    if (stt_config.language_hint == 1 &&
        stt_config.language_source[0] != '\0' &&
        stt_config.language_target[0] != '\0') {
        if (translate_lang_row == NULL) {
            translate_lang_row = stt_view_create_plain_container(lang_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            floatair_assert(translate_lang_row != NULL, "translate_lang_row NULL");
            container_set_layout_hbox_spaced(translate_lang_row, TRANSLATE_LANG_HINT_GAP);
            container_set_align(translate_lang_row,
                                CONTAINER_ALIGN_CENTER,
                                CONTAINER_ALIGN_CENTER,
                                CONTAINER_ALIGN_CENTER);
        }

        lang_row_obj = container_get_obj(translate_lang_row);
        if (translate_lable_lang_source == NULL) {
            translate_lable_lang_source = stt_view_create_text_label(lang_row_obj,
                                                                     LV_SIZE_CONTENT,
                                                                     LV_SIZE_CONTENT,
                                                                     "",
                                                                     LABEL_ALIGN_CENTER,
                                                                     LABEL_OVERFLOW_SCROLL_CIRCULAR);
            floatair_assert(translate_lable_lang_source != NULL, "translate_lable_lang_source NULL");
            stt_view_apply_lang_hint_text_theme(translate_lable_lang_source,
                                                TRANSLATE_LANG_HINT_GAP,
                                                stt_config.sourceTextDirection == TEXT_DIRECTION_RTL
                                                    ? LV_BASE_DIR_RTL
                                                    : LV_BASE_DIR_LTR);
        }
        if (translate_img_lang_hint == NULL) {
            translate_img_lang_hint = img_create(lang_row_obj, NULL);
            floatair_assert(translate_img_lang_hint != NULL, "translate_img_lang_hint NULL");
            img_set_src(translate_img_lang_hint, FLOATAIR_SYS_IMG("switch.jpg"));
        }
        if (translate_lable_lang_target == NULL) {
            translate_lable_lang_target = stt_view_create_text_label(lang_row_obj,
                                                                     LV_SIZE_CONTENT,
                                                                     LV_SIZE_CONTENT,
                                                                     "",
                                                                     LABEL_ALIGN_CENTER,
                                                                     LABEL_OVERFLOW_SCROLL_CIRCULAR);
            floatair_assert(translate_lable_lang_target != NULL, "translate_lable_lang_target NULL");
            stt_view_apply_lang_hint_text_theme(translate_lable_lang_target,
                                                TRANSLATE_LANG_HINT_GAP,
                                                stt_config.targetTextDirection == TEXT_DIRECTION_RTL
                                                    ? LV_BASE_DIR_RTL
                                                    : LV_BASE_DIR_LTR);
        }

        lv_obj_move_to_index(ui_widget_get_obj(UI_WIDGET(translate_img_lang_hint)), 1);

        src_set_text_start_us = (uint32_t)GetTimeUs();
        label_set_text(translate_lable_lang_source, stt_config.language_source);
        src_set_text_cost_us = (uint32_t)GetTimeUs() - src_set_text_start_us;
        dst_set_text_start_us = (uint32_t)GetTimeUs();
        label_set_text(translate_lable_lang_target, stt_config.language_target);
        dst_set_text_cost_us = (uint32_t)GetTimeUs() - dst_set_text_start_us;
        stt_view_apply_lang_hint_theme(translate_lable_lang_source,
                                       TRANSLATE_LANG_HINT_GAP,
                                       stt_config.sourceTextDirection == TEXT_DIRECTION_RTL
                                           ? LV_BASE_DIR_RTL
                                           : LV_BASE_DIR_LTR);
        stt_view_apply_lang_hint_theme(translate_lable_lang_target,
                                       TRANSLATE_LANG_HINT_GAP,
                                       stt_config.targetTextDirection == TEXT_DIRECTION_RTL
                                           ? LV_BASE_DIR_RTL
                                           : LV_BASE_DIR_LTR);

        ui_widget_set_visible(UI_WIDGET(translate_lang), true);
        ui_widget_set_size(UI_WIDGET(translate_lable_lang_source),
                           LV_SIZE_CONTENT,
                           LV_SIZE_CONTENT);
        ui_widget_set_size(UI_WIDGET(translate_lable_lang_target),
                           LV_SIZE_CONTENT,
                           LV_SIZE_CONTENT);
        lang_layout_start_us = (uint32_t)GetTimeUs();
        lv_obj_update_layout(lang_row_obj);
        lang_layout_cost_us = (uint32_t)GetTimeUs() - lang_layout_start_us;
        if (translate_content != NULL) {
            content_layout_start_us = (uint32_t)GetTimeUs();
            lv_obj_update_layout(container_get_obj(translate_content));
            content_layout_cost_us = (uint32_t)GetTimeUs() - content_layout_start_us;
        }
        floatair_info(
            "translate lang hint timing: src_set=%luus/%lums dst_set=%luus/%lums lang_layout=%luus/%lums content_layout=%luus/%lums visible=1",
            (unsigned long)src_set_text_cost_us,
            (unsigned long)(src_set_text_cost_us / 1000U),
            (unsigned long)dst_set_text_cost_us,
            (unsigned long)(dst_set_text_cost_us / 1000U),
            (unsigned long)lang_layout_cost_us,
            (unsigned long)(lang_layout_cost_us / 1000U),
            (unsigned long)content_layout_cost_us,
            (unsigned long)(content_layout_cost_us / 1000U));
        return;
    }

    ui_widget_set_visible(UI_WIDGET(translate_lang), false);
    if (translate_content != NULL) {
        content_layout_start_us = (uint32_t)GetTimeUs();
        lv_obj_update_layout(container_get_obj(translate_content));
        content_layout_cost_us = (uint32_t)GetTimeUs() - content_layout_start_us;
    }
    floatair_info("translate lang hint timing: src_set=0us/0ms dst_set=0us/0ms lang_layout=0us/0ms content_layout=%luus/%lums visible=0",
                  (unsigned long)content_layout_cost_us,
                  (unsigned long)(content_layout_cost_us / 1000U));
}

/**
 * @brief 页面加载回调。
 *
 * @param[in] root 页面根对象。
 * @param[in] data 页面入参。
 * @return 无返回值。
 */
static void translate_page_create(lv_obj_t* root, const app_page_data_t* data) {
    lv_obj_t* status_bar = NULL;
    lv_obj_t* content_obj = NULL;
    lv_obj_t* lang_obj = NULL;
    lv_obj_t* scroll_obj = NULL;

    (void)data;
    floatair_info("entry");
    floatair_assert(root != NULL, "root NULL");
    translate_root = root;

    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }

    stt_style_init();

    translate_init_img = stt_view_create_center_image(root);
    floatair_assert(translate_init_img != NULL, "translate_init_img NULL");

    translate_init_label = stt_view_create_text_label(root,
                                                      LV_PCT(100),
                                                      LV_SIZE_CONTENT,
                                                      "",
                                                      LABEL_ALIGN_CENTER,
                                                      LABEL_OVERFLOW_WRAP);
    floatair_assert(translate_init_label != NULL, "translate_init_label NULL");
    stt_view_apply_text_theme(translate_init_label, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    lv_obj_align_to(label_get_obj(translate_init_label),
                    ui_widget_get_obj(UI_WIDGET(translate_init_img)),
                    LV_ALIGN_OUT_BOTTOM_MID,
                    0,
                    10);

    translate_content = stt_view_create_plain_container(root, LV_PCT(100), LV_PCT(100));
    floatair_assert(translate_content != NULL, "translate_content NULL");
    container_set_layout_vbox(translate_content);
    container_set_align(translate_content,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START);
    container_set_padding_box(translate_content, 0, 0, TRANSLATE_SCROLL_TOP_MARGIN, 0);
    content_obj = container_get_obj(translate_content);

    translate_lang = stt_view_create_plain_container(content_obj, LV_PCT(100), LV_SIZE_CONTENT);
    floatair_assert(translate_lang != NULL, "translate_lang NULL");
    container_set_layout_hbox(translate_lang);
    container_set_align(translate_lang,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);
    container_set_padding(translate_lang, 0, 3);
    lang_obj = container_get_obj(translate_lang);
    lv_obj_set_style_min_height(lang_obj, STATUS_BAR_IMG_H, 0);

    translate_notice_op = stt_view_create_text_label(root,
                                                     LV_PCT(100),
                                                     stt_get_font_height(),
                                                     "",
                                                     LABEL_ALIGN_CENTER,
                                                     LABEL_OVERFLOW_WRAP);
    floatair_assert(translate_notice_op != NULL, "translate_notice_op NULL");
    stt_view_apply_text_theme(translate_notice_op, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    lv_obj_align(label_get_obj(translate_notice_op), LV_ALIGN_BOTTOM_MID, 0, 0);

    floatair_assert(translate_status_bar_ensure_widgets(), "translate status bar widgets are NULL");

    translate_scroll = stt_view_create_plain_container(content_obj, LV_PCT(100), 0);
    floatair_assert(translate_scroll != NULL, "translate_scroll NULL");
    scroll_obj = container_get_obj(translate_scroll);
    container_set_child_grow(scroll_obj, 1);
    container_set_layout_vbox_spaced(translate_scroll, get_system_font_row_space());
    container_set_align(translate_scroll,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START);
    container_set_padding_box(translate_scroll,
                              TRANSLATE_TEXT_SIDE_PADDING,
                              TRANSLATE_TEXT_SIDE_PADDING,
                              0,
                              0);
    container_set_scrollable(translate_scroll, true);
    container_set_scroll_dir(translate_scroll, LV_DIR_VER);
    container_set_scrollbar_mode(translate_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_move_foreground(content_obj);
    lv_obj_move_foreground(label_get_obj(translate_notice_op));
    translate_stt_init_rows();

    if (app_router_get_entry_mode() == APP_ROUTER_ENTRY_LOCAL) {
        translate_mode_go_description();
    } else if (app_router_get_entry_mode() == APP_ROUTER_ENTRY_REMOTE) {
        translate_mode_go_wait();
    } else {
        translate_mode_go_none();
    }

    floatair_info("Done");
}

static void translate_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    (void)system_request_keyword_spotting_enabled(false);
    system_status_bar_set_mode(true);
    floatair_assert(translate_status_bar_ensure_widgets(), "translate status bar widgets are NULL");
    translate_view_sync_layout();
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
    translate_stt_update();
}

/**
 * @brief 页面卸载完成回调。
 *
 * @return 无返回值。
 */
static void translate_page_destroy(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    (void)system_request_keyword_spotting_enabled(system_config_get_keyword_spotting_enabled());

    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }

    translate_init_label = NULL;
    translate_init_img = NULL;
    translate_root = NULL;
    translate_notice_op = NULL;
    translate_audio_source = NULL;
    translate_mic_direction = NULL;
    translate_waveicon = NULL;
    translate_content = NULL;
    translate_lang = NULL;
    translate_lang_row = NULL;
    translate_lable_lang_source = NULL;
    translate_lable_lang_target = NULL;
    translate_img_lang_hint = NULL;
    translate_scroll = NULL;
    translate_scroll_spacer = NULL;
    memset(translate_stt_rows, 0, sizeof(translate_stt_rows));
    translate_mode = VIEW_MODE_FUNC_NONE;
}

static app_page_t s_translate_page = {
    .name = APP_NAME_TRANSLATE,
    .on_create = translate_page_create,
    .on_appear = translate_page_appear,
    .on_disappear = NULL,
    .on_destroy = translate_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* translate_page_get(void) {
    return &s_translate_page;
}
