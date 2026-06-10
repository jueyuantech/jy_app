/**
 * @file assistant_view.c
 * @brief Assistant 弹窗视图实现。
 */
#include "assistant.h"

#include "assistant_popup_ui.h"
#include "app_def.h"
#include "stt_view_common.h"
#include "common/app_framework/app_layers.h"
#include "common/widgets/container.h"
#include "common/widgets/label.h"
#include "system/stt_common.h"
#include "system/system_def.h"

#include <string.h>

#define ASSISTANT_DEFAULT_SPACER " "
typedef enum {
    ASSISTANT_AREA_ANSWER = 0,   ///< assistant 回答区域。
    ASSISTANT_AREA_QUESTION = 1, ///< 用户提问区域。
} assistant_area_t;

typedef enum {
    ASSISTANT_TEXT_ROLE_NONE = 0,     ///< 无明确文本角色。
    ASSISTANT_TEXT_ROLE_QUESTION = 1, ///< 当前更新为提问文本。
    ASSISTANT_TEXT_ROLE_ANSWER = 2,   ///< 当前更新为回答文本。
} assistant_text_role_t;

static assistant_popup_ui_t s_ui;
static bool s_report_close_on_delete = true; ///< 删除 popup 时是否主动上报 assistant 关闭。
static assistant_text_role_t s_last_update_role = ASSISTANT_TEXT_ROLE_NONE; ///< 最近一次文本更新角色。

/**
 * @brief 应用 assistant 文本标签样式。
 * @param[in,out] label 目标标签。
 * @param[in] emphasize 是否使用强调样式。
 * @return 无返回值。
 */
static void assistant_apply_label_theme(label_t* label, bool emphasize) {
    if (label == NULL) {
        return;
    }

    lv_obj_t* obj = label_get_obj(label);

    stt_view_apply_text_theme(label, LABEL_ALIGN_LEFT, LABEL_OVERFLOW_WRAP);
    if (obj == NULL) {
        return;
    }

    if (emphasize) {
        lv_obj_add_style(obj, &stt_stylecur, LV_PART_MAIN);
    } else {
        lv_obj_add_style(obj, &stt_stylehis, LV_PART_MAIN);
        lv_obj_set_style_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    }
}

/**
 * @brief 查找当前问答区域最新一条非空文本下标。
 * @param[in] area assistant 文本区域编号。
 * @return 成功返回 STT 缓冲下标，失败返回 -1。
 */
static int assistant_find_latest_index_by_area(uint32_t area) {
    int size = stt_size();

    for (int i = 0; i < size; i++) {
        const char* text = NULL;
        uint32_t entry_area = stt_buffer_get_area_by_index((size_t)i);

        if (entry_area != area) {
            continue;
        }

        text = stt_buffer_get_transcribe_by_index((size_t)i);
        if (text != NULL && text[0] != '\0') {
            return i;
        }
    }

    return -1;
}

/**
 * @brief 记录最近一次 assistant 文本更新应落到当前问答的哪个区域。
 * @param[in] area 协议文本区域。
 * @return 无返回值。
 */
void assistant_stt_note_update(uint8_t area) {
    assistant_text_role_t role = ASSISTANT_TEXT_ROLE_NONE;

    if (area == ASSISTANT_AREA_QUESTION) {
        role = ASSISTANT_TEXT_ROLE_QUESTION;
    } else if (area == ASSISTANT_AREA_ANSWER) {
        role = ASSISTANT_TEXT_ROLE_ANSWER;
    }

    s_last_update_role = role;
}

/**
 * @brief 刷新 assistant 当前问答文本。
 * @return 无返回值。
 */
static void assistant_refresh_labels(void) {
    uint32_t refresh_start_us = (uint32_t)GetTimeUs();
    uint32_t refresh_end_us = 0;
    uint32_t question_set_start_us = 0;
    uint32_t question_set_cost_us = 0;
    uint32_t answer_set_start_us = 0;
    uint32_t answer_set_cost_us = 0;
    int question_index = assistant_find_latest_index_by_area(ASSISTANT_AREA_QUESTION);
    int answer_index = assistant_find_latest_index_by_area(ASSISTANT_AREA_ANSWER);
    const char* question = question_index >= 0
                               ? stt_buffer_get_transcribe_by_index((size_t)question_index)
                               : NULL;
    const char* answer = NULL;
    const char* next_question = (question != NULL && question[0] != '\0')
                                    ? question
                                    : ASSISTANT_DEFAULT_SPACER;

    if (s_ui.scroll == NULL) {
        return;
    }

    if (answer_index >= 0 && (question_index < 0 || answer_index <= question_index)) {
        answer = stt_buffer_get_transcribe_by_index((size_t)answer_index);
    }

    if (s_ui.question_label != NULL) {
        question_set_start_us = (uint32_t)GetTimeUs();
        stt_view_update_incremental_text(s_ui.question_label, next_question);
        question_set_cost_us = (uint32_t)GetTimeUs() - question_set_start_us;
    }
    if (s_ui.answer_label != NULL) {
        const char* next_answer = (answer == NULL || answer[0] == '\0')
                                      ? ASSISTANT_DEFAULT_SPACER
                                      : answer;
        answer_set_start_us = (uint32_t)GetTimeUs();
        stt_view_update_incremental_text(s_ui.answer_label, next_answer);
        answer_set_cost_us = (uint32_t)GetTimeUs() - answer_set_start_us;
    }

    container_scroll_to_bottom(s_ui.scroll, LV_ANIM_OFF);
    refresh_end_us = (uint32_t)GetTimeUs();
    floatair_info("assistant refresh timing: question_set=%luus/%lums answer_set=%luus/%lums total=%luus/%lums q_idx=%d a_idx=%d role=%d",
                  (unsigned long)question_set_cost_us,
                  (unsigned long)(question_set_cost_us / 1000U),
                  (unsigned long)answer_set_cost_us,
                  (unsigned long)(answer_set_cost_us / 1000U),
                  (unsigned long)(refresh_end_us - refresh_start_us),
                  (unsigned long)((refresh_end_us - refresh_start_us) / 1000U),
                  question_index,
                  answer_index,
                  (int)s_last_update_role);
}

/**
 * @brief 刷新 assistant 文本。
 * @return 无返回值。
 */
void assistant_stt_update(void) {
    if (!assistant_is_open()) {
        return;
    }

    assistant_refresh_labels();
}

/**
 * @brief 清空 assistant STT 缓冲并刷新默认文本。
 * @return 无返回值。
 */
void assistant_stt_clear(void) {
    stt_buffer_init();
    s_last_update_role = ASSISTANT_TEXT_ROLE_NONE;
    assistant_refresh_labels();
}

/**
 * @brief 字体配置变化后刷新 assistant 文本样式。
 * @return 无返回值。
 */
static void assistant_on_fontconfig_changed(void) {
    stt_style_init();
    assistant_apply_label_theme(s_ui.question_label, true);
    assistant_apply_label_theme(s_ui.answer_label, false);

    if (s_ui.scroll) {
        lv_obj_t* scroll_obj = container_get_obj(s_ui.scroll);
        lv_obj_set_style_pad_row(scroll_obj, get_system_font_row_space() + 10, 0);
    }

    assistant_refresh_labels();
}

/**
 * @brief 处理 assistant popup 删除事件并通知生命周期层清理资源。
 * @param[in] event LVGL 删除事件对象。
 * @return 无返回值。
 */
static void assistant_popup_delete_event_handle(lv_event_t* event) {
    (void)event;
    bool report_close = s_report_close_on_delete;
    memset(&s_ui, 0, sizeof(s_ui));
    s_last_update_role = ASSISTANT_TEXT_ROLE_NONE;
    assistant_on_popup_deleted(report_close);
}

/**
 * @brief 获取 assistant popup 父对象。
 * @return 返回可挂载父对象；失败时返回当前屏幕。
 */
static lv_obj_t* assistant_get_popup_parent(void) {
    lv_obj_t* parent = app_layers_get_popup();

    if (parent != NULL && lv_obj_is_valid(parent)) {
        return parent;
    }

    return lv_screen_active();
}

/**
 * @brief 查询 assistant popup 视图是否已创建并有效。
 * @return `true` 表示正在显示，`false` 表示未显示。
 */
bool assistant_is_open(void) {
    lv_obj_t* root = container_get_obj(s_ui.root);

    return root != NULL && lv_obj_is_valid(root);
}

/**
 * @brief 将输入事件转发给 assistant popup。
 * @param[in] code LVGL 事件码。
 * @return `true` 表示事件已消费，`false` 表示未消费。
 */
bool assistant_handle_event(lv_event_code_t code) {
    if (!assistant_is_open()) {
        return false;
    }

    switch (code) {
        case LV_EVENT_CLICKED:
        case LV_EVENT_LONG_PRESSED:
            system_report_touch_event(code);
            return true;
        case LV_EVENT_DCLICKED:
            (void)assistant_close(true);
            return true;
        case LV_EVENT_GESTURE_LEFT:
            if (s_ui.scroll) {
                container_scroll_up(s_ui.scroll, 3.0f / 4.0f);
            }
            return true;
        case LV_EVENT_GESTURE_RIGHT:
            if (s_ui.scroll) {
                container_scroll_down(s_ui.scroll, 3.0f / 4.0f);
            }
            return true;
        default:
            return false;
    }
}

/**
 * @brief 在 app framework popup 层创建 assistant 视图。
 * @return `true` 表示创建成功，`false` 表示创建失败。
 */
bool assistant_popup_open(void) {
    lv_obj_t* parent = assistant_get_popup_parent();

    if (assistant_is_open()) {
        lv_obj_move_foreground(container_get_obj(s_ui.root));
        return true;
    }

    if (parent == NULL) {
        floatair_err("assistant popup parent NULL");
        return false;
    }

    floatair_assert(assistant_popup_init_ui(parent, &s_ui), "assistant popup init failed");
    lv_obj_t* root = container_get_obj(s_ui.root);
    floatair_assert(root != NULL, "assistant popup root NULL");

    lv_obj_add_event_cb(root, assistant_popup_delete_event_handle, LV_EVENT_DELETE, NULL);

    assistant_on_fontconfig_changed();
    lv_obj_move_foreground(root);
    return true;
}

/**
 * @brief 销毁 assistant popup 视图。
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return `true` 表示销毁成功，`false` 表示销毁失败。
 */
bool assistant_popup_close(bool report_close) {
    if (!assistant_is_open()) {
        memset(&s_ui, 0, sizeof(s_ui));
        s_last_update_role = ASSISTANT_TEXT_ROLE_NONE;
        return true;
    }

    s_report_close_on_delete = report_close;
    lv_obj_delete(container_get_obj(s_ui.root));
    s_report_close_on_delete = true;
    return true;
}
