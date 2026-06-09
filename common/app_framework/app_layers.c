/**
 * @file app_layers.c
 * @brief 应用框架固定 UI 层级实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-21
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#include "common/app_framework/app_layers.h"

#include "app_def.h"
#include "common/app_framework/app_stereo.h"
#include "lvgl/src/core/lv_refr.h"
#include "lvgl/src/draw/lv_draw.h"
#include "system/system_runtime_types.h"

#include <limits.h>
#include <string.h>

typedef struct {
    lv_obj_t* screen;      ///< 当前屏幕根对象
    lv_obj_t* root;        ///< app 框架业务根层
    lv_obj_t* background;  ///< 背景层
    lv_obj_t* app;         ///< 应用页面层
    lv_obj_t* app_float;   ///< 应用内容浮层
    lv_obj_t* overlay;     ///< 系统遮罩层
    lv_obj_t* popup;       ///< 弹窗层
    lv_obj_t* top;         ///< 最高优先级 UI 层
    int32_t width;         ///< 层级宽度
    int32_t height;        ///< 层级高度
    int32_t root_offset_x; ///< 根层当前横向业务位置
    int32_t root_offset_y; ///< 根层当前纵向业务位置
    int32_t app_float_offset_x; ///< 应用内容浮层当前横向视差位置
    int32_t popup_offset_x;///< 弹窗层当前横向视差位置
} app_layers_state_t;

static app_layers_state_t g_layers = {
    .root_offset_x = INT32_MIN,
    .root_offset_y = INT32_MIN,
    .app_float_offset_x = INT32_MIN,
    .popup_offset_x = INT32_MIN,
};

/**
 * @brief 判断对象是否有效。
 * @param[in] obj 待检查对象。
 * @return `true` 表示对象非空且有效。
 */
static bool app_layers_obj_valid(lv_obj_t* obj) {
    return obj != NULL && lv_obj_is_valid(obj);
}

/**
 * @brief 判断现有层级对象是否仍可复用。
 * @param[in] root 期望的根对象。
 * @return `true` 表示可以复用，`false` 表示需要重建。
 */
static bool app_layers_is_reusable(lv_obj_t* root) {
    return g_layers.screen == root &&
           g_layers.screen != NULL &&
           lv_obj_is_valid(g_layers.screen) &&
           app_layers_obj_valid(g_layers.root) &&
           app_layers_obj_valid(g_layers.background) &&
           app_layers_obj_valid(g_layers.app) &&
           app_layers_obj_valid(g_layers.app_float) &&
           app_layers_obj_valid(g_layers.overlay) &&
           app_layers_obj_valid(g_layers.popup) &&
           app_layers_obj_valid(g_layers.top);
}

/**
 * @brief 删除现有层级对象并清空状态。
 * @return 无返回值。
 */
static void app_layers_reset(void) {
    if (app_layers_obj_valid(g_layers.root)) lv_obj_delete(g_layers.root);
    g_layers = (app_layers_state_t){
        .root_offset_x = INT32_MIN,
        .root_offset_y = INT32_MIN,
        .app_float_offset_x = INT32_MIN,
        .popup_offset_x = INT32_MIN,
    };
}

/**
 * @brief 配置单个层对象的基础样式。
 * @param[in] layer 目标层对象。
 * @param[in] width 层宽度。
 * @param[in] height 层高度。
 * @return 无返回值。
 */
static void app_layers_prepare_layer(lv_obj_t* layer, int32_t width, int32_t height) {
    if (layer == NULL) {
        return;
    }

    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, width, height);
    lv_obj_align(layer, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_border_width(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(layer, 0, 0);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_CLICK_FOCUSABLE);
}

/**
 * @brief 创建一个固定层对象。
 * @param[in] parent 父对象。
 * @param[in] width 层宽度。
 * @param[in] height 层高度。
 * @return 创建成功返回层对象；失败返回 `NULL`。
 */
static lv_obj_t* app_layers_create_layer(lv_obj_t* parent, int32_t width, int32_t height) {
    lv_obj_t* layer = lv_obj_create(parent);
    if (layer == NULL) {
        return NULL;
    }

    app_layers_prepare_layer(layer, width, height);
    return layer;
}

/**
 * @brief 按目标眼位计算并应用根层、浮层位置。
 * @param[in] eye 目标眼位。
 * @return 无返回值。
 */
static void app_layers_apply_eye_scene(app_stereo_eye_t eye) {
    int32_t root_offset_x = (int32_t)SYSTEM_LCD_UI_X_BEGIN;
    int32_t root_offset_y = (int32_t)SYSTEM_LCD_UI_Y_BEGIN;
    int32_t app_float_offset_x = app_stereo_node_pos_x_trans(eye, FLOATAIR_APP_FLOAT_DISTANCE, 0);
    int32_t popup_offset_x = app_stereo_node_pos_x_trans(eye, FLOATAIR_POPUP_FLOAT_DISTANCE, 0);

    if (app_layers_obj_valid(g_layers.root)) {
        lv_obj_align(g_layers.root, LV_ALIGN_TOP_LEFT, root_offset_x, root_offset_y);
    }

    if (app_layers_obj_valid(g_layers.app_float)) {
        lv_obj_align(g_layers.app_float, LV_ALIGN_CENTER, app_float_offset_x, 0);
    }

    if (app_layers_obj_valid(g_layers.popup)) {
        lv_obj_align(g_layers.popup, LV_ALIGN_CENTER, popup_offset_x, 0);
    }

    if (eye == APP_STEREO_EYE_LEFT) {
        g_layers.root_offset_x = root_offset_x;
        g_layers.root_offset_y = root_offset_y;
        g_layers.app_float_offset_x = app_float_offset_x;
        g_layers.popup_offset_x = popup_offset_x;
    }
}

/**
 * @brief 等待指定图层的绘制任务完成。
 * @param[in] disp LVGL display。
 * @param[in,out] layer 待派发的绘制图层。
 * @return 无返回值。
 */
static void app_layers_wait_draw_layer(lv_display_t* disp, lv_layer_t* layer) {
    if (disp == NULL || layer == NULL) {
        return;
    }

    layer->all_tasks_added = true;
    while (layer->draw_task_head != NULL) {
        lv_draw_dispatch_wait_for_request();
        if (!lv_draw_dispatch_layer(disp, layer)) {
            lv_draw_dispatch_request();
        }
        lv_draw_dispatch();
    }
    lv_draw_wait_for_finish();
}

/**
 * @brief 清空 draw buffer 中当前区域的像素。
 * @param[in,out] buf 待清空的 draw buffer。
 * @param[in] width 区域宽度。
 * @param[in] height 区域高度。
 * @param[in] px_size 单像素字节数。
 * @return 无返回值。
 */
static void app_layers_clear_draw_buf(lv_draw_buf_t* buf, int32_t width, int32_t height, uint32_t px_size) {
    size_t row_bytes = 0;

    if (buf == NULL || buf->data == NULL || width <= 0 || height <= 0 || px_size == 0u || buf->header.stride == 0u) {
        return;
    }

    row_bytes = (size_t)width * px_size;
    for (int32_t y = 0; y < height; y++) {
        memset(buf->data + (size_t)y * buf->header.stride, 0, row_bytes);
    }
}

/**
 * @brief 将指定对象按横纵位移绘制到右眼 draw buffer。
 * @param[in] disp LVGL display。
 * @param[in] dst_buf 右眼 draw buffer。
 * @param[in] local_area 右眼本地坐标系中需要绘制的区域。
 * @param[in] obj 待绘制对象。
 * @param[in] shift_x 横向位移。
 * @param[in] shift_y 纵向位移。
 * @return 无返回值。
 */
static void app_layers_redraw_obj_to_eye(lv_display_t* disp,
                                         lv_draw_buf_t* dst_buf,
                                         const lv_area_t* local_area,
                                         lv_obj_t* obj,
                                         int32_t shift_x,
                                         int32_t shift_y) {
    lv_layer_t layer;
    lv_area_t layer_area;

    if (disp == NULL || dst_buf == NULL || local_area == NULL || !app_layers_obj_valid(obj) ||
        lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_area_set(&layer_area,
                local_area->x1 - shift_x,
                local_area->y1 - shift_y,
                local_area->x2 - shift_x,
                local_area->y2 - shift_y);

    memset(&layer, 0, sizeof(layer));
    layer.draw_buf = dst_buf;
    layer.buf_area = layer_area;
    layer._clip_area = layer_area;
    layer.phy_clip_area = layer_area;
    layer.color_format = (lv_color_format_t)dst_buf->header.cf;
#if LV_DRAW_TRANSFORM_USE_MATRIX
    lv_matrix_identity(&layer.matrix);
#endif

    lv_obj_redraw(&layer, obj);
    app_layers_wait_draw_layer(disp, &layer);
}

/**
 * @brief 将分层对象绘制到右眼指定区域。
 * @param[in] disp LVGL display。
 * @param[in,out] right_buf 右眼目标 draw buffer。
 * @param[in] local_area 右眼本地坐标系中需要绘制的区域。
 * @return 无返回值。
 */
static void app_layers_redraw_right_eye_layers(lv_display_t* disp, lv_draw_buf_t* right_buf, const lv_area_t* local_area) {
    int32_t app_float_left_x = 0;
    int32_t app_float_right_x = 0;
    int32_t app_float_shift_x = 0;
    int32_t popup_left_x = 0;
    int32_t popup_right_x = 0;
    int32_t popup_shift_x = 0;
    int32_t right_origin_x = 0;
    int32_t right_origin_y = 0;

    if (disp == NULL || right_buf == NULL || local_area == NULL) {
        return;
    }

    app_stereo_get_eye_origin(APP_STEREO_EYE_RIGHT, &right_origin_x, &right_origin_y);
    app_float_left_x = app_stereo_node_pos_x_trans(APP_STEREO_EYE_LEFT, FLOATAIR_APP_FLOAT_DISTANCE, 0);
    app_float_right_x = app_stereo_node_pos_x_trans(APP_STEREO_EYE_RIGHT, FLOATAIR_APP_FLOAT_DISTANCE, 0);
    app_float_shift_x = app_float_right_x - app_float_left_x;
    popup_left_x = app_stereo_node_pos_x_trans(APP_STEREO_EYE_LEFT, FLOATAIR_POPUP_FLOAT_DISTANCE, 0);
    popup_right_x = app_stereo_node_pos_x_trans(APP_STEREO_EYE_RIGHT, FLOATAIR_POPUP_FLOAT_DISTANCE, 0);
    popup_shift_x = popup_right_x - popup_left_x;

    app_layers_redraw_obj_to_eye(disp, right_buf, local_area, g_layers.background, 0, 0);
    app_layers_redraw_obj_to_eye(disp, right_buf, local_area, g_layers.app, 0, 0);
    app_layers_redraw_obj_to_eye(disp, right_buf, local_area, g_layers.app_float, app_float_shift_x, 0);
    app_layers_redraw_obj_to_eye(disp, right_buf, local_area, g_layers.popup, popup_shift_x, 0);
    app_layers_redraw_obj_to_eye(disp, right_buf, local_area, g_layers.overlay, 0, 0);
    app_layers_redraw_obj_to_eye(disp, right_buf, local_area, g_layers.top, 0, 0);
    app_layers_redraw_obj_to_eye(disp, right_buf, local_area, lv_display_get_layer_top(disp), -right_origin_x, -right_origin_y);
    app_layers_redraw_obj_to_eye(disp, right_buf, local_area, lv_display_get_layer_sys(disp), -right_origin_x, -right_origin_y);
}

/**
 * @brief 判断区域是否包含在另一个区域内。
 * @param[in] inner 被检查的区域。
 * @param[in] outer 期望包含它的区域。
 * @return `true` 表示完全包含。
 */
static bool app_layers_area_is_in(const lv_area_t* inner, const lv_area_t* outer) {
    if (inner == NULL || outer == NULL) {
        return false;
    }

    return inner->x1 >= outer->x1 &&
           inner->y1 >= outer->y1 &&
           inner->x2 <= outer->x2 &&
           inner->y2 <= outer->y2;
}

/**
 * @brief 计算两个区域的交集。
 * @param[out] result 输出交集区域。
 * @param[in] first 第一个区域。
 * @param[in] second 第二个区域。
 * @return `true` 表示存在交集。
 */
static bool app_layers_area_intersect(lv_area_t* result, const lv_area_t* first, const lv_area_t* second) {
    if (result == NULL || first == NULL || second == NULL) {
        return false;
    }

    result->x1 = LV_MAX(first->x1, second->x1);
    result->y1 = LV_MAX(first->y1, second->y1);
    result->x2 = LV_MIN(first->x2, second->x2);
    result->y2 = LV_MIN(first->y2, second->y2);
    return result->x1 <= result->x2 && result->y1 <= result->y2;
}

/**
 * @brief app framework 右眼分层渲染回调。
 * @param[in] disp LVGL display。
 * @param[in,out] buf 当前输出 draw buffer。
 * @param[in] flush_area 本次 flush 的显示区域。
 * @param[in] eye 目标眼位。
 * @return `true` 表示已经完成右眼渲染，`false` 表示应回退到 framebuffer 复制。
 */
static bool app_layers_render_eye_cb(lv_display_t* disp,
                                     lv_draw_buf_t* buf,
                                     const lv_area_t* flush_area,
                                     app_stereo_eye_t eye) {
    lv_draw_buf_t right_buf;
    int32_t eye_w = app_stereo_get_eye_frame_width();
    int32_t eye_h = app_stereo_get_eye_frame_height();
    int32_t right_origin_x = 0;
    int32_t right_origin_y = 0;
    uint32_t px_size = 0;
    size_t right_data_size = 0;
    lv_area_t right_eye_area;
    lv_area_t right_flush_area;
    lv_area_t right_local_area;
    int32_t buf_origin_x = 0;
    int32_t buf_origin_y = 0;
    size_t right_data_offset = 0;

    if (eye != APP_STEREO_EYE_RIGHT || disp == NULL || buf == NULL || buf->data == NULL ||
        !app_layers_obj_valid(g_layers.root)) {
        return false;
    }

    if (buf->header.stride == 0u) {
        return false;
    }

    px_size = lv_color_format_get_size((lv_color_format_t)buf->header.cf);
    if (px_size == 0u) {
        return false;
    }

    app_stereo_get_eye_origin(APP_STEREO_EYE_RIGHT, &right_origin_x, &right_origin_y);
    lv_area_set(&right_eye_area,
                right_origin_x,
                right_origin_y,
                right_origin_x + eye_w - 1,
                right_origin_y + eye_h - 1);

    if ((int32_t)buf->header.w == app_stereo_get_output_width() &&
        (int32_t)buf->header.h == app_stereo_get_output_height()) {
        lv_area_set(&right_local_area, 0, 0, eye_w - 1, eye_h - 1);
        right_data_size = ((size_t)eye_h - 1u) * buf->header.stride + (size_t)eye_w * px_size;
        if (buf->data_size <
            (size_t)right_origin_y * buf->header.stride + (size_t)right_origin_x * px_size + right_data_size) {
            return false;
        }

        right_buf = *buf;
        right_buf.header.w = (uint32_t)eye_w;
        right_buf.header.h = (uint32_t)eye_h;
        right_buf.data = buf->data + (size_t)right_origin_y * buf->header.stride + (size_t)right_origin_x * px_size;
        right_buf.data_size = (uint32_t)right_data_size;
        app_layers_clear_draw_buf(&right_buf, eye_w, eye_h, px_size);
        app_layers_redraw_right_eye_layers(disp, &right_buf, &right_local_area);
        return true;
    }

    if (flush_area == NULL ||
        !app_layers_area_intersect(&right_flush_area, flush_area, &right_eye_area) ||
        !app_layers_area_is_in(&right_flush_area, flush_area)) {
        return false;
    }

    if ((int32_t)buf->header.w != lv_area_get_width(flush_area) ||
        (int32_t)buf->header.h != lv_area_get_height(flush_area)) {
        return false;
    }

    buf_origin_x = flush_area->x1;
    buf_origin_y = flush_area->y1;
    right_local_area = right_flush_area;
    lv_area_move(&right_local_area, -right_origin_x, -right_origin_y);
    right_data_offset = (size_t)(right_flush_area.y1 - buf_origin_y) * buf->header.stride +
                        (size_t)(right_flush_area.x1 - buf_origin_x) * px_size;
    right_data_size = ((size_t)lv_area_get_height(&right_flush_area) - 1u) * buf->header.stride +
                      (size_t)lv_area_get_width(&right_flush_area) * px_size;
    if (right_data_offset + right_data_size > buf->data_size) {
        return false;
    }

    right_buf = *buf;
    right_buf.header.w = (uint32_t)lv_area_get_width(&right_flush_area);
    right_buf.header.h = (uint32_t)lv_area_get_height(&right_flush_area);
    right_buf.data = buf->data + right_data_offset;
    right_buf.data_size = (uint32_t)right_data_size;
    app_layers_clear_draw_buf(&right_buf,
                              lv_area_get_width(&right_flush_area),
                              lv_area_get_height(&right_flush_area),
                              px_size);
    app_layers_redraw_right_eye_layers(disp, &right_buf, &right_local_area);
    return true;
}

bool app_layers_init(lv_obj_t* screen_root, int32_t width, int32_t height) {
    lv_obj_t* root = (screen_root != NULL) ? screen_root : lv_screen_active();

    if (root == NULL || width <= 0 || height <= 0) {
        return false;
    }

    if (app_layers_is_reusable(root)) {
        app_layers_resize(width, height);
        return true;
    }
    if (g_layers.root != NULL) {
        app_layers_reset();
    }

    g_layers.root = app_layers_create_layer(root, width, height);
    g_layers.background = app_layers_create_layer(g_layers.root, width, height);
    g_layers.app = app_layers_create_layer(g_layers.root, width, height);
    g_layers.app_float = app_layers_create_layer(g_layers.root, width, height);
    g_layers.popup = app_layers_create_layer(g_layers.root, width, height);
    g_layers.overlay = app_layers_create_layer(g_layers.root, width, height);
    g_layers.top = app_layers_create_layer(g_layers.root, width, height);

    if (g_layers.background == NULL ||
        g_layers.root == NULL ||
        g_layers.app == NULL ||
        g_layers.app_float == NULL ||
        g_layers.overlay == NULL ||
        g_layers.popup == NULL ||
        g_layers.top == NULL) {
        if (g_layers.root != NULL) lv_obj_delete(g_layers.root);
        g_layers = (app_layers_state_t){
            .root_offset_x = INT32_MIN,
            .root_offset_y = INT32_MIN,
            .app_float_offset_x = INT32_MIN,
            .popup_offset_x = INT32_MIN,
        };
        return false;
    }

    g_layers.screen = root;
    g_layers.width = width;
    g_layers.height = height;
    app_stereo_set_render_eye_cb(app_layers_render_eye_cb);
    app_layers_resize(width, height);
    return true;
}

void app_layers_resize(int32_t width, int32_t height) {
    int32_t root_offset_x = 0;
    int32_t root_offset_y = 0;
    int32_t app_float_offset_x = 0;
    int32_t popup_offset_x = 0;

    if (width <= 0 || height <= 0) {
        return;
    }

    root_offset_x = (int32_t)SYSTEM_LCD_UI_X_BEGIN;
    root_offset_y = (int32_t)SYSTEM_LCD_UI_Y_BEGIN;
    app_float_offset_x = app_stereo_node_pos_x_trans(APP_STEREO_EYE_LEFT, FLOATAIR_APP_FLOAT_DISTANCE, 0);
    popup_offset_x = app_stereo_node_pos_x_trans(APP_STEREO_EYE_LEFT, FLOATAIR_POPUP_FLOAT_DISTANCE, 0);

    if (g_layers.width == width &&
        g_layers.height == height &&
        g_layers.root_offset_x == root_offset_x &&
        g_layers.root_offset_y == root_offset_y &&
        g_layers.app_float_offset_x == app_float_offset_x &&
        g_layers.popup_offset_x == popup_offset_x) {
        return;
    }

    g_layers.width = width;
    g_layers.height = height;
    g_layers.root_offset_x = root_offset_x;
    g_layers.root_offset_y = root_offset_y;
    g_layers.app_float_offset_x = app_float_offset_x;
    g_layers.popup_offset_x = popup_offset_x;
    app_layers_prepare_layer(g_layers.background, width, height);
    app_layers_prepare_layer(g_layers.root, width, height);
    app_layers_prepare_layer(g_layers.app, width, height);
    app_layers_prepare_layer(g_layers.app_float, width, height);
    app_layers_prepare_layer(g_layers.overlay, width, height);
    app_layers_prepare_layer(g_layers.popup, width, height);
    app_layers_prepare_layer(g_layers.top, width, height);

    app_layers_apply_eye_scene(APP_STEREO_EYE_LEFT);

    if (g_layers.background != NULL) lv_obj_move_foreground(g_layers.background);
    if (g_layers.app != NULL) lv_obj_move_foreground(g_layers.app);
    if (g_layers.app_float != NULL) lv_obj_move_foreground(g_layers.app_float);
    if (g_layers.popup != NULL) lv_obj_move_foreground(g_layers.popup);
    if (g_layers.overlay != NULL) lv_obj_move_foreground(g_layers.overlay);
    if (g_layers.top != NULL) lv_obj_move_foreground(g_layers.top);
}

lv_obj_t* app_layers_get_background(void) {
    return g_layers.background;
}

lv_obj_t* app_layers_get_app(void) {
    return g_layers.app;
}

lv_obj_t* app_layers_get_app_float(void) {
    return g_layers.app_float;
}

lv_obj_t* app_layers_get_overlay(void) {
    return g_layers.overlay;
}

lv_obj_t* app_layers_get_popup(void) {
    return g_layers.popup;
}

lv_obj_t* app_layers_get_top(void) {
    return g_layers.top;
}
