/**
 * @file stt_view_common.h
 * @brief STT 文本视图公共辅助接口
 */
#pragma once

#include "common/widgets/container.h"
#include "common/widgets/img.h"
#include "common/widgets/label.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief STT 文本在转写/翻译场景下的最大显示长度。
 */
#define STT_VIEW_TEXT_MAX_LEN 128U

/**
 * @brief 创建通用文本组件。
 *
 * @param parent 父对象。
 * @param width 组件宽度。
 * @param height 组件高度。
 * @param text 初始文本。
 * @param align 文本对齐方式。
 * @param overflow 文本溢出策略。
 * @return 创建成功返回文本组件句柄，失败返回 `NULL`。
 */
label_t* stt_view_create_text_label(lv_obj_t* parent,
                                    lv_coord_t width,
                                    lv_coord_t height,
                                    const char* text,
                                    label_align_t align,
                                    label_overflow_t overflow);

/**
 * @brief 创建透明容器。
 *
 * @param parent 父对象。
 * @param width 容器宽度。
 * @param height 容器高度。
 * @return 创建成功返回容器句柄，失败返回 `NULL`。
 */
container_t* stt_view_create_plain_container(lv_obj_t* parent,
                                             lv_coord_t width,
                                             lv_coord_t height);

/**
 * @brief 创建连接等待图标。
 *
 * @param parent 父对象。
 * @return 创建成功返回图片组件句柄，失败返回 `NULL`。
 */
img_t* stt_view_create_center_image(lv_obj_t* parent);

/**
 * @brief 应用说明类文本样式。
 *
 * @param label 目标文本组件。
 * @param align 文本对齐方式。
 * @param overflow 文本溢出策略。
 * @return 无返回值。
 */
void stt_view_apply_text_theme(label_t* label,
                               label_align_t align,
                               label_overflow_t overflow);

/**
 * @brief 应用语言提示标签的静态文本样式。
 *
 * 该接口会刷新字体、字间距、行间距等较重的文本样式，
 * 只应在控件创建或字体配置变化时调用。
 *
 * @param label 目标文本组件。
 * @param pad_hor 左右内边距。
 * @param base_dir 文本方向。
 * @return 无返回值。
 */
void stt_view_apply_lang_hint_text_theme(label_t* label, int32_t pad_hor, lv_base_dir_t base_dir);

/**
 * @brief 应用语言提示标签样式。
 *
 * @param label 目标文本组件。
 * @param pad_hor 左右内边距。
 * @param base_dir 文本方向。
 * @return 无返回值。
 */
void stt_view_apply_lang_hint_theme(label_t* label, int32_t pad_hor, lv_base_dir_t base_dir);

/**
 * @brief 应用 STT 文本标签的静态文本样式。
 *
 * 该接口会刷新字体、字间距、行间距等较重的文本样式，
 * 只应在控件创建或字体配置变化时调用。
 *
 * @param label 目标文本组件。
 * @param base_dir 文本方向。
 * @return 无返回值。
 */
void stt_view_apply_stt_label_text_theme(label_t* label, lv_base_dir_t base_dir);

/**
 * @brief 应用 STT 文本标签样式。
 *
 * @param label 目标文本组件。
 * @param base_dir 文本方向。
 * @param is_bottom_most 是否为当前高亮内容。
 * @return 无返回值。
 */
void stt_view_apply_stt_label_theme(label_t* label, lv_base_dir_t base_dir, bool is_bottom_most);

/**
 * @brief 以 STT 协议的前缀增长语义刷新文本。
 *
 * 若 `text` 以前一帧文本为前缀，则仅追加新增尾部；
 * 否则回退到整段覆盖，兼容协议回改或历史切换场景。
 *
 * @param label 目标文本组件。
 * @param text 最新完整文本。
 * @return 无返回值。
 */
void stt_view_update_incremental_text(label_t* label, const char* text);

/**
 * @brief 以 STT 协议的前缀增长语义刷新文本，并限制最大显示长度。
 *
 * 文本超过 `STT_VIEW_TEXT_MAX_LEN` 时保留末尾内容，并从合法 UTF-8 边界开始
 * 写入 label，避免转写/翻译页持续增长到过长文本。
 *
 * @param label 目标文本组件。
 * @param text 最新完整文本。
 * @return 无返回值。
 */
void stt_view_update_incremental_text_max_128(label_t* label, const char* text);

/**
 * @brief 更新状态栏波形图标显示状态。
 *
 * @param waveicon 波形图标对象。
 * @param show `true` 表示显示，`false` 表示隐藏。
 * @return 无返回值。
 */
void stt_view_update_waveicon(lv_obj_t* waveicon, bool show);

/**
 * @brief 刷新麦克风方向图标。
 *
 * @param mic_direction 麦克风方向图标对象。
 * @return 无返回值。
 */
void stt_view_update_mic_direction(lv_obj_t* mic_direction);

/**
 * @brief 刷新音频来源图标。
 *
 * @param audio_source 音频来源图标对象。
 * @return 无返回值。
 */
void stt_view_update_audio_source(lv_obj_t* audio_source);

#ifdef __cplusplus
}
#endif
