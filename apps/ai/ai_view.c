/**
 * @file ai_view.c
 * @brief AI 应用旧版助手页面视图实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_ai
 */
#include "ai.h"
#include "home/home.h"
#include "stt_view_common.h"

#include "common/app_framework/app_manager.h"
#include "common/widgets/container.h"
#include "common/widgets/img.h"
#include "common/widgets/label.h"
#include "common/widgets/status_bar.h"
#include "floatair_fs.h"
#include "system/stt_common.h"
#include "system/system_def.h"
#include "system/system_runtime_ui.h"

#include <string.h>

/** AI 文本区域左右边距。 */
#define AI_TEXT_SIDE_PADDING 12
/** AI 对话气泡内边距。 */
#define AI_BUBBLE_TEXT_PADDING 10
/** AI 对话行问答头像尺寸。 */
#define AI_BUBBLE_ICON_SIZE 32
/** AI 对话行头像与气泡间距。 */
#define AI_BUBBLE_ICON_SPACING 8
/** AI 对话行之间的垂直间距。 */
#define AI_BUBBLE_ROW_SPACING 10

/**
 * @brief AI 页面展示模式。
 */
typedef enum {
    AI_VIEW_MODE_NONE = 0, ///< 页面内容全部隐藏。
    AI_VIEW_MODE_INIT,     ///< 展示 AI 引导态。
    AI_VIEW_MODE_STT,      ///< 展示 AI 对话文本。
} ai_view_mode_t;

/**
 * @brief AI 对话文本区域。
 */
typedef enum {
    AI_STT_AREA_ANSWER = 0,   ///< 回答文本。
    AI_STT_AREA_QUESTION = 1, ///< 提问文本。
} ai_stt_area_t;

/**
 * @brief AI STT 消息类型。
 */
typedef enum {
    AI_STT_MSG_TYPE_STT = 0, ///< 正式对话文本。
    AI_STT_MSG_TYPE_HINT = 2, ///< 提示文本。
} ai_stt_msg_type_t;

/**
 * @brief AI 最近一次文本更新角色。
 */
typedef enum {
    AI_STT_UPDATE_ROLE_NONE = 0,     ///< 无明确更新角色。
    AI_STT_UPDATE_ROLE_QUESTION = 1, ///< 最近更新为提问。
    AI_STT_UPDATE_ROLE_ANSWER = 2,   ///< 最近更新为回答。
} ai_stt_update_role_t;

/**
 * @brief AI 单条 STT 行视图缓存。
 */
typedef struct {
    container_t* row; ///< 行根容器。
    img_t* left_icon; ///< 回答区域左侧头像。
    label_t* label;   ///< 文本标签。
    img_t* right_icon; ///< 提问区域右侧头像。
} ai_stt_row_t;

static ai_view_mode_t s_ai_view_mode = AI_VIEW_MODE_NONE; ///< 当前页面展示模式。
static container_t* s_init_box = NULL;                    ///< 初始化态根容器。
static label_t* s_init_label = NULL;                      ///< 初始化提示文本。
static img_t* s_init_img = NULL;                          ///< 初始化提示图标。
static lv_obj_t* s_audio_source = NULL;                   ///< 状态栏音频来源图标。
static lv_obj_t* s_mic_direction = NULL;                  ///< 状态栏麦克风方向图标。
static lv_obj_t* s_waveicon = NULL;                       ///< 状态栏波形图标。
static container_t* s_scroll = NULL;                      ///< STT 文本滚动容器。
static container_t* s_scroll_spacer = NULL;               ///< STT 文本顶部弹性撑开项。
static ai_stt_row_t s_rows[STT_INFO_MAX_MSG_NUM];         ///< STT 行缓存。
static lv_obj_t* s_ai_root = NULL;                        ///< AI 页面根对象。
static ai_stt_update_role_t s_last_update_role = AI_STT_UPDATE_ROLE_NONE; ///< 最近一次文本更新角色。

static bool ai_status_bar_ensure_widgets(void);
static void ai_sync_scroll_viewport(void);

/**
 * @brief 按 STT area 获取行文本最大宽度。
 * @param[in] area STT area，0 表示左侧，1 表示右侧。
 * @return 返回对应 area 的行文本宽度。
 */
static lv_coord_t ai_get_text_width(uint32_t area) {
    if (area == 1) {
        return PAGE_SST_RIGHT_WIDTH;
    }

    return PAGE_SST_LEFT_WIDTH;
}

/**
 * @brief 获取当前 STT 文本基准方向。
 * @return 返回 LVGL 文本基准方向。
 */
static lv_base_dir_t ai_get_text_base_dir(void) {
    return stt_config.sourceTextDirection == TEXT_DIRECTION_RTL ? LV_BASE_DIR_RTL : LV_BASE_DIR_LTR;
}

/**
 * @brief 应用 AI 对话气泡样式。
 * @param[in,out] label 目标文本组件。
 * @return 无返回值。
 */
static void ai_apply_text_bubble_style(label_t* label) {
    if (label == NULL) {
        return;
    }

    label_set_radius(label, 10);
    label_set_border_width(label, 2);
    label_set_padding(label, AI_BUBBLE_TEXT_PADDING, AI_BUBBLE_TEXT_PADDING);
}

/**
 * @brief 创建 AI 对话行一侧头像。
 * @param[in] parent 父对象。
 * @param[in] src 图片资源。
 * @return 创建成功返回图片组件句柄，失败返回 `NULL`。
 */
static img_t* ai_stt_create_row_icon(lv_obj_t* parent, const void* src) {
    img_cfg_t cfg = img_default_cfg();

    cfg.w = AI_BUBBLE_ICON_SIZE;
    cfg.h = AI_BUBBLE_ICON_SIZE;
    cfg.src = src;
    return img_create(parent, &cfg);
}

/**
 * @brief 确保 AI 对话行问答头像和文本组件已按布局顺序创建。
 * @param[in,out] row 目标行缓存。
 * @param[in] row_obj 行根 LVGL 对象。
 * @param[in] align 文本对齐方式。
 * @param[in] base_dir 文本基准方向。
 * @return 无返回值。
 */
static void ai_stt_ensure_row_widgets(ai_stt_row_t* row,
                                      lv_obj_t* row_obj,
                                      label_align_t align,
                                      lv_base_dir_t base_dir) {
    if (row == NULL || row_obj == NULL) {
        return;
    }

    if (row->left_icon == NULL) {
        row->left_icon = ai_stt_create_row_icon(row_obj, FLOATAIR_SYS_IMG("icon_robot.jpg"));
        floatair_assert(row->left_icon != NULL, "ai stt left icon NULL");
    }
    if (row->label == NULL) {
        row->label = stt_view_create_text_label(row_obj,
                                                LV_SIZE_CONTENT,
                                                LV_SIZE_CONTENT,
                                                "",
                                                align,
                                                LABEL_OVERFLOW_WRAP);
        floatair_assert(row->label != NULL, "ai stt label NULL");
        stt_view_apply_stt_label_text_theme(row->label, base_dir);
    }
    if (row->right_icon == NULL) {
        row->right_icon = ai_stt_create_row_icon(row_obj, FLOATAIR_SYS_IMG("icon_robot2.jpg"));
        floatair_assert(row->right_icon != NULL, "ai stt right icon NULL");
    }
}

/**
 * @brief 按文本区域刷新 AI 对话行头像显示位置。
 * @param[in,out] row 目标行缓存。
 * @param[in] area STT area，0 表示左侧回答，1 表示右侧提问。
 * @return 无返回值。
 */
static void ai_stt_update_row_icons(ai_stt_row_t* row, uint32_t area) {
    if (row == NULL) {
        return;
    }

    ui_widget_set_visible(UI_WIDGET(row->left_icon), area != 1);
    ui_widget_set_visible(UI_WIDGET(row->right_icon), area == 1);
}

/**
 * @brief 隐藏底部状态栏 AI 自定义图标。
 * @return 无返回值。
 */
static void ai_hide_status_widgets(void) {
    if (s_audio_source != NULL && lv_obj_is_valid(s_audio_source)) {
        lv_obj_add_flag(s_audio_source, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_mic_direction != NULL && lv_obj_is_valid(s_mic_direction)) {
        lv_obj_add_flag(s_mic_direction, LV_OBJ_FLAG_HIDDEN);
    }
    stt_view_update_waveicon(s_waveicon, false);
}

/**
 * @brief 显示并刷新底部状态栏 AI 自定义图标。
 * @return 无返回值。
 */
static void ai_show_status_widgets(void) {
    if (!ai_status_bar_ensure_widgets()) {
        return;
    }
    if (s_audio_source != NULL && lv_obj_is_valid(s_audio_source)) {
        lv_obj_remove_flag(s_audio_source, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_mic_direction != NULL && lv_obj_is_valid(s_mic_direction)) {
        lv_obj_remove_flag(s_mic_direction, LV_OBJ_FLAG_HIDDEN);
    }
    stt_view_update_audio_source(s_audio_source);
    stt_view_update_mic_direction(s_mic_direction);
    stt_view_update_waveicon(s_waveicon, true);
}

static bool ai_status_bar_widgets_valid(void) {
    return s_audio_source != NULL &&
           lv_obj_is_valid(s_audio_source) &&
           s_waveicon != NULL &&
           lv_obj_is_valid(s_waveicon) &&
           s_mic_direction != NULL &&
           lv_obj_is_valid(s_mic_direction);
}

static bool ai_status_bar_ensure_widgets(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        s_audio_source = NULL;
        s_waveicon = NULL;
        s_mic_direction = NULL;
        return false;
    }
    if (!ai_status_bar_widgets_valid()) {
        status_bar_clear_custom_widgets(status_bar);
        s_audio_source = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("sound_phone.jpg"), STATUS_BAR_WIDGET_ALIGN_LEFT);
        s_waveicon = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("sound_wave.jpg"), STATUS_BAR_WIDGET_ALIGN_RIGHT);
        s_mic_direction = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("micphone.jpg"), STATUS_BAR_WIDGET_ALIGN_RIGHT);
    }

    return ai_status_bar_widgets_valid();
}

/**
 * @brief 同步 AI 对话滚动视口高度，避免底部状态栏遮挡最后一行。
 * @return 无返回值。
 */
static void ai_sync_scroll_viewport(void) {
    lv_obj_t* scroll_obj = NULL;
    lv_coord_t content_h = system_ui_get_page_content_height();

    if (s_scroll == NULL || content_h <= 0) {
        return;
    }

    scroll_obj = container_get_obj(s_scroll);
    if (scroll_obj == NULL || !lv_obj_is_valid(scroll_obj)) {
        return;
    }

    lv_obj_set_size(scroll_obj, LV_PCT(100), content_h);
}

/**
 * @brief 切换到 AI 引导态。
 * @return 无返回值。
 */
static void ai_mode_go_init(void) {
    s_ai_view_mode = AI_VIEW_MODE_INIT;
    ui_widget_set_visible(UI_WIDGET(s_init_box), true);
    ui_widget_set_visible(UI_WIDGET(s_init_label), true);
    ui_widget_set_visible(UI_WIDGET(s_init_img), true);
    ui_widget_set_visible(UI_WIDGET(s_scroll), false);
    ai_hide_status_widgets();

    if (s_init_img != NULL) {
        img_set_src(s_init_img, FLOATAIR_SYS_IMG("icon_robot.jpg"));
    }

    if (s_init_label != NULL) {
        label_set_text(s_init_label, app_get_str("AI_ASK"));
    }
}

/**
 * @brief 切换到 AI 文本展示态。
 * @return 无返回值。
 */
static void ai_mode_go_stt(void) {
    s_ai_view_mode = AI_VIEW_MODE_STT;
    ui_widget_set_visible(UI_WIDGET(s_init_box), false);
    ui_widget_set_visible(UI_WIDGET(s_init_label), false);
    ui_widget_set_visible(UI_WIDGET(s_init_img), false);
    ai_sync_scroll_viewport();
    ui_widget_set_visible(UI_WIDGET(s_scroll), true);
    ai_show_status_widgets();
}

/**
 * @brief 初始化单条 AI 文本行缓存。
 * @param[in,out] row 目标行缓存。
 * @param[in] parent 父对象。
 * @return 无返回值。
 */
static void ai_stt_init_row(ai_stt_row_t* row, lv_obj_t* parent) {
    if (row == NULL || parent == NULL || row->row != NULL) {
        return;
    }

    row->row = stt_view_create_plain_container(parent, LV_PCT(100), LV_SIZE_CONTENT);
    floatair_assert(row->row != NULL, "ai stt row NULL");
    container_set_layout_hbox_spaced(row->row, AI_BUBBLE_ICON_SPACING);
    container_set_align(row->row,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);
    row->left_icon = NULL;
    row->label = NULL;
    row->right_icon = NULL;
    ui_widget_set_visible(UI_WIDGET(row->row), false);
}

/**
 * @brief 创建 AI 文本行缓存。
 * @return 无返回值。
 */
static void ai_stt_init_rows(void) {
    lv_obj_t* scroll_obj = NULL;

    if (s_scroll == NULL) {
        return;
    }

    scroll_obj = container_get_obj(s_scroll);
    if (scroll_obj == NULL) {
        return;
    }

    if (s_scroll_spacer == NULL) {
        s_scroll_spacer = stt_view_create_plain_container(scroll_obj, LV_PCT(100), 0);
        floatair_assert(s_scroll_spacer != NULL, "ai scroll spacer NULL");
        container_set_child_grow(container_get_obj(s_scroll_spacer), 1);
    }

    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        ai_stt_init_row(&s_rows[i], scroll_obj);
    }
}

/**
 * @brief 隐藏所有 AI 文本行缓存。
 * @return 无返回值。
 */
static void ai_stt_hide_all_rows(void) {
    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (s_rows[i].row != NULL) {
            ui_widget_set_visible(UI_WIDGET(s_rows[i].row), false);
        }
    }
}

/**
 * @brief 查找指定区域和类型的最近一条对话文本。
 * @param[in] area AI 对话文本区域。
 * @param[in] msg_type STT 消息类型。
 * @return 找到返回 STT 缓冲下标，失败返回 -1。
 */
static int ai_find_latest_index_by_area(uint32_t area, uint32_t msg_type) {
    int size = stt_size();

    for (int i = 0; i < size; i++) {
        const char* text = NULL;
        uint32_t entry_area = stt_buffer_get_area_by_index((size_t)i);
        uint32_t entry_msg_type = stt_buffer_get_msg_type_by_index((size_t)i);

        if (entry_area != area || entry_msg_type != msg_type) {
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
 * @brief 查找最近一条提示文本。
 * @return 找到返回 STT 缓冲下标，失败返回 -1。
 */
static int ai_find_latest_hint_index(void) {
    int size = stt_size();

    for (int i = 0; i < size; i++) {
        const char* text = NULL;
        uint32_t msg_type = stt_buffer_get_msg_type_by_index((size_t)i);

        if (msg_type != AI_STT_MSG_TYPE_HINT) {
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
 * @brief 记录 AI STT 文本更新对应的对话区域。
 * @param[in] area 文本区域，0 为回答、1 为提问。
 * @param[in] msg_type 消息类型，0 为正式文本。
 * @return 无返回值。
 */
void ai_stt_note_update(uint8_t area, uint8_t msg_type) {
    if (msg_type == AI_STT_MSG_TYPE_HINT || area == AI_STT_AREA_QUESTION) {
        s_last_update_role = AI_STT_UPDATE_ROLE_QUESTION;
    } else if (area == AI_STT_AREA_ANSWER && msg_type == AI_STT_MSG_TYPE_STT) {
        s_last_update_role = AI_STT_UPDATE_ROLE_ANSWER;
    }
}

/**
 * @brief 刷新单条 AI 文本行。
 * @param[in,out] row 目标行缓存。
 * @param[in] text 文本内容。
 * @param[in] area STT area，0 表示左侧，1 表示右侧。
 * @param[in] is_bottom_most 是否为当前高亮内容。
 * @return 无返回值。
 */
static void ai_stt_update_row(ai_stt_row_t* row,
                              const char* text,
                              uint32_t area,
                              bool is_bottom_most) {
    uint32_t set_text_start_us = 0;
    uint32_t set_text_cost_us = 0;
    uint32_t layout_start_us = 0;
    uint32_t layout_cost_us = 0;
    lv_obj_t* row_obj = NULL;
    lv_obj_t* label_obj = NULL;
    lv_base_dir_t base_dir = ai_get_text_base_dir();
    label_align_t align = area == 1 ? LABEL_ALIGN_RIGHT : LABEL_ALIGN_LEFT;
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
    container_set_align(row->row,
                        area == 1 ? CONTAINER_ALIGN_END : CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);

    ai_stt_ensure_row_widgets(row, row_obj, align, base_dir);
    ai_stt_update_row_icons(row, area);

    label_set_align(row->label, align);
    ai_apply_text_bubble_style(row->label);
    label_obj = label_get_obj(row->label);
    ui_widget_set_size(UI_WIDGET(row->label), LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    if (label_obj != NULL) {
        lv_obj_set_style_max_width(label_obj, ai_get_text_width(area), LV_PART_MAIN);
    }
    set_text_start_us = (uint32_t)GetTimeUs();
    stt_view_update_incremental_text(row->label, text);
    set_text_cost_us = (uint32_t)GetTimeUs() - set_text_start_us;
    ui_widget_set_visible(UI_WIDGET(row->label), true);
    stt_view_apply_stt_label_theme(row->label, base_dir, is_bottom_most);
    if (!is_bottom_most) {
        label_set_opacity(row->label, LV_OPA_COVER);
        if (label_obj != NULL) {
            lv_obj_set_style_opa(label_obj, LV_OPA_COVER, LV_PART_MAIN);
        }
    }
    layout_start_us = (uint32_t)GetTimeUs();
    lv_obj_update_layout(row_obj);
    layout_cost_us = (uint32_t)GetTimeUs() - layout_start_us;
    floatair_info("ai row timing: set_text=%luus/%lums layout=%luus/%lums area=%lu bottom=%d",
                  (unsigned long)set_text_cost_us,
                  (unsigned long)(set_text_cost_us / 1000U),
                  (unsigned long)layout_cost_us,
                  (unsigned long)(layout_cost_us / 1000U),
                  (unsigned long)area,
                  is_bottom_most ? 1 : 0);
}

/**
 * @brief 刷新 AI STT 文本显示。
 * @return 无返回值。
 */
void ai_stt_update(void) {
    uint32_t update_start_us = (uint32_t)GetTimeUs();
    uint32_t update_end_us = 0;
    size_t visible_row_count = 0;
    int question_index = -1;
    int question_hint_index = -1;
    int answer_index = -1;
    const char* question = NULL;
    const char* answer = NULL;
    lv_obj_t* scroll_obj = NULL;

    ai_show_status_widgets();

    if (s_scroll == NULL || container_get_obj(s_scroll) == NULL) {
        return;
    }
    scroll_obj = container_get_obj(s_scroll);

    if (stt_size() == 0) {
        ai_stt_hide_all_rows();
        ui_widget_set_visible(UI_WIDGET(s_scroll), false);
        ai_mode_go_init();
        return;
    }

    ai_stt_init_rows();
    question_index = ai_find_latest_index_by_area(AI_STT_AREA_QUESTION, AI_STT_MSG_TYPE_STT);
    question_hint_index = ai_find_latest_hint_index();
    answer_index = ai_find_latest_index_by_area(AI_STT_AREA_ANSWER, AI_STT_MSG_TYPE_STT);
    if (s_last_update_role == AI_STT_UPDATE_ROLE_QUESTION &&
        question_hint_index >= 0 &&
        (question_index < 0 || question_hint_index < question_index)) {
        question_index = question_hint_index;
    }
    if (question_index >= 0) {
        question = stt_buffer_get_transcribe_by_index((size_t)question_index);
    }
    if (answer_index >= 0 && (question_index < 0 || answer_index <= question_index)) {
        answer = stt_buffer_get_transcribe_by_index((size_t)answer_index);
    }
    if (s_last_update_role == AI_STT_UPDATE_ROLE_QUESTION) {
        answer = NULL;
    }

    if (question != NULL && question[0] != '\0') {
        ai_stt_update_row(&s_rows[visible_row_count], question, AI_STT_AREA_QUESTION, answer == NULL);
        ++visible_row_count;
    }
    if (answer != NULL && answer[0] != '\0' && visible_row_count < STT_INFO_MAX_MSG_NUM) {
        ai_stt_update_row(&s_rows[visible_row_count], answer, AI_STT_AREA_ANSWER, true);
        ++visible_row_count;
    }

    if (visible_row_count == 0) {
        update_end_us = (uint32_t)GetTimeUs();
        floatair_info("ai refresh timing: total=%luus/%lums rows=0 q_idx=%d q_hint_idx=%d a_idx=%d role=%d",
                      (unsigned long)(update_end_us - update_start_us),
                      (unsigned long)((update_end_us - update_start_us) / 1000U),
                      question_index,
                      question_hint_index,
                      answer_index,
                      (int)s_last_update_role);
        return;
    }

    if (s_ai_view_mode != AI_VIEW_MODE_STT) {
        ai_mode_go_stt();
    }

    for (size_t i = visible_row_count; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (s_rows[i].row != NULL) {
            ui_widget_set_visible(UI_WIDGET(s_rows[i].row), false);
        }
    }

    container_scroll_to_bottom(s_scroll, LV_ANIM_OFF);
    if (scroll_obj != NULL) {
        lv_obj_invalidate(scroll_obj);
    }
    update_end_us = (uint32_t)GetTimeUs();
    floatair_info("ai refresh timing: total=%luus/%lums rows=%lu q_idx=%d q_hint_idx=%d a_idx=%d",
                  (unsigned long)(update_end_us - update_start_us),
                  (unsigned long)((update_end_us - update_start_us) / 1000U),
                  (unsigned long)visible_row_count,
                  question_index,
                  question_hint_index,
                  answer_index);
}

/**
 * @brief 清空 AI STT 文本显示。
 * @return 无返回值。
 */
void ai_stt_clear(void) {
    stt_buffer_init();
    s_last_update_role = AI_STT_UPDATE_ROLE_NONE;
    if (s_scroll != NULL) {
        ai_stt_hide_all_rows();
        ui_widget_set_visible(UI_WIDGET(s_scroll), false);
    }

    ai_mode_go_init();
}

/**
 * @brief 处理 AI 字体配置变更。
 * @return 无返回值。
 */
void ai_on_fontconfig_changed(void) {
    lv_base_dir_t base_dir = ai_get_text_base_dir();

    stt_style_init();
    stt_view_apply_text_theme(s_init_label, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (s_rows[i].label != NULL) {
            stt_view_apply_stt_label_text_theme(s_rows[i].label, base_dir);
            ai_apply_text_bubble_style(s_rows[i].label);
        }
    }

    ai_stt_update();
}

/**
 * @brief 页面触控事件处理。
 * @param[in] event LVGL 事件对象。
 * @return 无返回值。
 */
static void touch_event_handle(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    bool can_scroll = (s_ai_view_mode == AI_VIEW_MODE_STT &&
                       s_scroll != NULL &&
                       stt_size() > 0 &&
                       stt_buffer_get_is_final_by_index(0));

    switch (code) {
    case LV_EVENT_GESTURE_LEFT:
        if (can_scroll) {
            container_scroll_up(s_scroll, 3.0f / 4.0f);
        }
        break;
    case LV_EVENT_GESTURE_RIGHT:
        if (can_scroll) {
            container_scroll_down(s_scroll, 3.0f / 4.0f);
        }
        break;
    case LV_EVENT_DCLICKED:
        (void)app_router_exit_current_app();
        break;
    default:
        break;
    }

    system_report_touch_event(code);
}

/**
 * @brief 创建 AI 页面内容。
 * @param[in] root 页面根对象。
 * @param[in] data 页面入参。
 * @return 无返回值。
 */
static void ai_page_create(lv_obj_t* root, const app_page_data_t* data) {
    lv_obj_t* status_bar = NULL;
    img_cfg_t init_img_cfg = img_default_cfg();

    (void)data;
    floatair_assert(root != NULL, "root NULL");
    s_ai_root = root;

    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }
    stt_style_init();

    s_init_box = stt_view_create_plain_container(root, LV_PCT(100), LV_SIZE_CONTENT);
    floatair_assert(s_init_box != NULL, "ai init box NULL");
    container_set_layout_vbox_spaced(s_init_box, 10);
    container_set_align(s_init_box,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);
    lv_obj_center(container_get_obj(s_init_box));

    init_img_cfg.w = LVGL_UI_ICONW_80;
    init_img_cfg.h = LVGL_UI_ICONH_80;
    s_init_img = img_create(container_get_obj(s_init_box), &init_img_cfg);
    floatair_assert(s_init_img != NULL, "ai init img NULL");

    s_init_label = stt_view_create_text_label(container_get_obj(s_init_box),
                                              LV_PCT(100),
                                              LV_SIZE_CONTENT,
                                              "",
                                              LABEL_ALIGN_CENTER,
                                              LABEL_OVERFLOW_WRAP);
    floatair_assert(s_init_label != NULL, "ai init label NULL");
    stt_view_apply_text_theme(s_init_label, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);

    floatair_assert(ai_status_bar_ensure_widgets(), "ai status bar widgets are NULL");

    s_scroll = stt_view_create_plain_container(root, LV_PCT(100), system_ui_get_page_content_height());
    floatair_assert(s_scroll != NULL, "ai scroll NULL");
    lv_obj_align(container_get_obj(s_scroll), LV_ALIGN_TOP_LEFT, 0, 0);
    container_set_layout_vbox_spaced(s_scroll, AI_BUBBLE_ROW_SPACING);
    container_set_align(s_scroll,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START);
    container_set_padding_box(s_scroll,
                              AI_TEXT_SIDE_PADDING,
                              AI_TEXT_SIDE_PADDING,
                              0,
                              0);
    container_set_scrollable(s_scroll, true);
    container_set_scroll_dir(s_scroll, LV_DIR_VER);
    container_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_AUTO);

    ai_stt_init_rows();

    ai_mode_go_init();
}

static void ai_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    system_status_bar_set_mode(true);
    floatair_assert(ai_status_bar_ensure_widgets(), "ai status bar widgets are NULL");
    ai_sync_scroll_viewport();
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
    ai_stt_update();
}

static void ai_page_destroy(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }
    stt_style_deinit();
    s_ai_root = NULL;
    s_init_box = NULL;
    s_init_label = NULL;
    s_init_img = NULL;
    s_audio_source = NULL;
    s_mic_direction = NULL;
    s_waveicon = NULL;
    s_scroll = NULL;
    s_scroll_spacer = NULL;
    memset(s_rows, 0, sizeof(s_rows));
    s_ai_view_mode = AI_VIEW_MODE_NONE;
    s_last_update_role = AI_STT_UPDATE_ROLE_NONE;
}

static app_page_t s_ai_page = {
    .name = APP_NAME_AI,
    .on_create = ai_page_create,
    .on_appear = ai_page_appear,
    .on_disappear = NULL,
    .on_destroy = ai_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* ai_page_get(void) {
    return &s_ai_page;
}
