#ifndef COMMON_WIDGETS_CONTAINER_H
#define COMMON_WIDGETS_CONTAINER_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "ui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用容器组件句柄。
 */
typedef struct container_t container_t;

/**
 * @brief 容器主轴方向。
 */
typedef enum {
    CONTAINER_FLOW_ROW = 0,   ///< HBox: 主轴为左右，交叉轴为上下。
    CONTAINER_FLOW_COLUMN,    ///< VBox: 主轴为上下，交叉轴为左右。
} container_flow_t;

/**
 * @brief 容器布局对齐方式。
 */
typedef enum {
    CONTAINER_ALIGN_START = 0,        ///< 从起始端对齐。
    CONTAINER_ALIGN_CENTER,           ///< 居中对齐。
    CONTAINER_ALIGN_END,              ///< 从结束端对齐。
    CONTAINER_ALIGN_SPACE_BETWEEN,    ///< 两端贴边，中间均分剩余空间。
    CONTAINER_ALIGN_SPACE_AROUND,     ///< 每个子项两侧保留等量空间。
    CONTAINER_ALIGN_SPACE_EVENLY,     ///< 所有空隙完全均分。
} container_align_t;

/**
 * @brief 容器高度自适应策略。
 */
typedef enum {
    CONTAINER_HEIGHT_POLICY_NONE = 0,          ///< 不自动调整最大高度。
    CONTAINER_HEIGHT_POLICY_CONTENT_MAX_PARENT ///< 高度按内容自适应，最大不超过父容器内容区。
} container_height_policy_t;

/**
 * @brief 容器组件配置项。
 */
typedef struct {
    int32_t x;                  ///< 容器左上角 X 坐标。
    int32_t y;                  ///< 容器左上角 Y 坐标。
    int32_t w;                  ///< 容器宽度。
    int32_t h;                  ///< 容器高度。
    int32_t max_w;              ///< 容器最大宽度；传 `LV_COORD_MAX` 表示不限制。
    int32_t max_h;              ///< 容器最大高度；传 `LV_COORD_MAX` 表示不限制。
    int32_t radius;             ///< 容器圆角半径。
    int32_t border_width;       ///< 容器边框宽度。
    int32_t pad_hor;            ///< 左右内边距。
    int32_t pad_ver;            ///< 上下内边距。
    uint8_t opa;                ///< 容器透明度，范围 0~255。
} container_cfg_t;

/**
 * @brief 获取默认的容器配置。
 *
 * @return 返回填充默认值后的配置结构体。
 */
container_cfg_t container_default_cfg(void);

/**
 * @brief 创建一个容器组件并挂载到指定父对象。
 *
 * @param parent 父对象；传 `NULL` 时退化到当前活动屏幕。
 * @param cfg 容器配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回容器句柄，失败返回 `NULL`。
 */
container_t* container_create(lv_obj_t* parent, const container_cfg_t* cfg);

/**
 * @brief 为容器追加样式。
 *
 * @param container 目标容器句柄。
 * @param style 目标样式。
 * @param selector LVGL 样式选择器。
 * @return 无返回值。
 */
void container_add_style(container_t* container, const lv_style_t* style, uint32_t selector);

/**
 * @brief 设置容器圆角半径。
 *
 * @param container 目标容器句柄。
 * @param radius 圆角半径。
 * @return 无返回值。
 */
void container_set_radius(container_t* container, int32_t radius);

/**
 * @brief 设置容器边框宽度。
 *
 * @param container 目标容器句柄。
 * @param border_width 边框宽度。
 * @return 无返回值。
 */
void container_set_border_width(container_t* container, int32_t border_width);

/**
 * @brief 设置容器透明度。
 *
 * @param container 目标容器句柄。
 * @param opa 新透明度，范围 0~255。
 * @return 无返回值。
 */
void container_set_opacity(container_t* container, uint8_t opa);

/**
 * @brief 设置容器最大宽度。
 *
 * @param container 目标容器句柄。
 * @param max_width 最大宽度；传 `LV_COORD_MAX` 表示不限制。
 * @return 无返回值。
 */
void container_set_max_width(container_t* container, int32_t max_width);

/**
 * @brief 设置容器最大高度。
 *
 * @param container 目标容器句柄。
 * @param max_height 最大高度；传 `LV_COORD_MAX` 表示不限制。
 * @return 无返回值。
 */
void container_set_max_height(container_t* container, int32_t max_height);

/**
 * @brief 设置容器内边距。
 *
 * @param container 目标容器句柄。
 * @param pad_hor 左右内边距。
 * @param pad_ver 上下内边距。
 * @return 无返回值。
 */
void container_set_padding(container_t* container, int32_t pad_hor, int32_t pad_ver);

/**
 * @brief 分别设置容器四边内边距。
 *
 * @param container 目标容器句柄。
 * @param pad_left 左侧内边距。
 * @param pad_right 右侧内边距。
 * @param pad_top 上侧内边距。
 * @param pad_bottom 下侧内边距。
 * @return 无返回值。
 */
void container_set_padding_box(container_t* container,
                               int32_t pad_left,
                               int32_t pad_right,
                               int32_t pad_top,
                               int32_t pad_bottom);

/**
 * @brief 将容器应用为 VBox 布局。
 *
 * @param container 目标容器句柄。
 * @return 无返回值。
 */
void container_set_layout_vbox(container_t* container);

/**
 * @brief 将容器应用为带间距的 VBox 布局。
 *
 * @param container 目标容器句柄。
 * @param spacing 子项间距。
 * @return 无返回值。
 */
void container_set_layout_vbox_spaced(container_t* container, int32_t spacing);

/**
 * @brief 将容器应用为 HBox 布局。
 *
 * @param container 目标容器句柄。
 * @return 无返回值。
 */
void container_set_layout_hbox(container_t* container);

/**
 * @brief 将容器应用为带间距的 HBox 布局。
 *
 * @param container 目标容器句柄。
 * @param spacing 子项间距。
 * @return 无返回值。
 */
void container_set_layout_hbox_spaced(container_t* container, int32_t spacing);

/**
 * @brief 设置容器主轴方向。
 *
 * @param container 目标容器句柄。
 * @param flow 主轴方向。
 * @return 无返回值。
 */
void container_set_flow(container_t* container, container_flow_t flow);

/**
 * @brief 设置容器布局对齐方式。
 *
 * @param container 目标容器句柄。
 * @param main_align 主轴对齐方式。
 * @param cross_align 交叉轴对齐方式，仅支持 `START/CENTER/END`。
 * @param track_align 多行/多列轨道对齐方式。
 * @return 无返回值。
 */
void container_set_align(container_t* container,
                         container_align_t main_align,
                         container_align_t cross_align,
                         container_align_t track_align);

/**
 * @brief 设置容器内子项间距。
 *
 * @param container 目标容器句柄。
 * @param spacing 子项之间的间距。
 * @return 无返回值。
 */
void container_set_spacing(container_t* container, int32_t spacing);

/**
 * @brief 设置容器是否可滚动。
 *
 * @param container 目标容器句柄。
 * @param scrollable `true` 表示允许滚动，`false` 表示关闭滚动。
 * @return 无返回值。
 */
void container_set_scrollable(container_t* container, bool scrollable);

/**
 * @brief 设置容器滚动方向。
 *
 * @param container 目标容器句柄。
 * @param dir 滚动方向。
 * @return 无返回值。
 */
void container_set_scroll_dir(container_t* container, lv_dir_t dir);

/**
 * @brief 设置容器滚动条模式。
 *
 * @param container 目标容器句柄。
 * @param mode 滚动条模式。
 * @return 无返回值。
 */
void container_set_scrollbar_mode(container_t* container, lv_scrollbar_mode_t mode);

/**
 * @brief 设置容器高度自适应策略。
 *
 * @param container 目标容器句柄。
 * @param policy 高度自适应策略。
 * @return 无返回值。
 */
void container_set_height_policy(container_t* container, container_height_policy_t policy);

/**
 * @brief 通知父容器某个子对象的布局尺寸已变化。
 *
 * 文本、图片等子项内容变化后调用该接口，容器会从直接父级开始逐级刷新布局，
 * 并重新应用声明过的高度自适应策略。
 *
 * @param child_layout_obj 布局发生变化的子对象。
 * @return 无返回值。
 */
void container_notify_child_layout_changed(lv_obj_t* child_layout_obj);

/**
 * @brief 将容器内容直接滚动到顶部。
 *
 * 调用前会先刷新容器布局。
 *
 * @param container 目标容器句柄。
 * @param anim_en 是否启用滚动动画。
 * @return 无返回值。
 */
void container_scroll_to_top(container_t* container, lv_anim_enable_t anim_en);

/**
 * @brief 将容器内容直接滚动到底部。
 *
 * 调用前会先刷新容器布局。
 *
 * @param container 目标容器句柄。
 * @param anim_en 是否启用滚动动画。
 * @return 无返回值。
 */
void container_scroll_to_bottom(container_t* container, lv_anim_enable_t anim_en);

/**
 * @brief 将容器内容按给定比例向上滚动。
 *
 * 调用前会先刷新容器布局；滚动步长为容器高度乘以 `step_ratio`，
 * 滚动带动画。
 *
 * @param container 目标容器句柄。
 * @param step_ratio 步长比例，传小于等于 0 时忽略本次滚动。
 * @return 无返回值。
 */
void container_scroll_up(container_t* container, float step_ratio);

/**
 * @brief 将容器内容按给定比例向下滚动。
 *
 * 调用前会先刷新容器布局；滚动步长为容器高度乘以 `step_ratio`，
 * 滚动带动画。
 *
 * @param container 目标容器句柄。
 * @param step_ratio 步长比例，传小于等于 0 时忽略本次滚动。
 * @return 无返回值。
 */
void container_scroll_down(container_t* container, float step_ratio);

/**
 * @brief 设置子项的 grow 权重。
 *
 * @param child 目标子项对象。
 * @param grow grow 值，0 表示不拉伸。
 * @return 无返回值。
 */
void container_set_child_grow(lv_obj_t* child, uint8_t grow);

/**
 * @brief 将子项宽高设置为父容器的 100%。
 *
 * @param child 目标子项对象。
 * @param fill_x `true` 表示宽度设为父容器的 100%。
 * @param fill_y `true` 表示高度设为父容器的 100%。
 * @return 无返回值。
 */
void container_set_child_fill(lv_obj_t* child, bool fill_x, bool fill_y);

/**
 * @brief 在 Row Flex 容器中，将目标子项宽度限制为当前剩余主轴空间。
 *
 * 常用于“图标 + 文本”这类布局：容器本身允许按内容收缩，但当总宽度触达
 * 容器可用上限时，目标子项会被压缩到剩余宽度，以便其内部文字自行换行。
 *
 * @param container 目标容器句柄，需为 Row Flex 容器。
 * @param child 需要限制宽度的子项对象。
 * @return 无返回值。
 */
void container_limit_child_width_to_remaining_row_space(container_t* container, lv_obj_t* child);

/**
 * @brief 判断容器句柄及底层对象是否有效。
 *
 * 仅可用于仍由调用方持有且尚未销毁的句柄；不要对已销毁句柄调用。
 *
 * @param container 目标容器句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
bool container_is_valid(container_t* container);

/**
 * @brief 批量应用容器配置。
 *
 * @param container 目标容器句柄。
 * @param cfg 配置结构；传 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
void container_apply_cfg(container_t* container, const container_cfg_t* cfg);

/**
 * @brief 获取底层 LVGL 对象。
 *
 * @param container 目标容器句柄。
 * @return 返回底层 `lv_obj_t*`；无效容器时返回 `NULL`。
 */
lv_obj_t* container_get_obj(container_t* container);

#ifdef __cplusplus
}
#endif

#endif
