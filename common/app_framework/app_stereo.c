/**
 * @file app_stereo.c
 * @brief app 框架双眼显示尺寸与偏移计算实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-05-02
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#include "common/app_framework/app_stereo.h"

#include "app_def.h"
#include "floatair_dbg.h"
#include "lvgl/src/core/lv_refr_private.h"
#include "lvgl/src/display/lv_display_private.h"
#include "lvgl/src/draw/lv_draw_buf.h"
#include "system/system_runtime_types.h"

#include <string.h>

typedef struct {
    app_stereo_output_mode_t output_mode; ///< 当前双眼输出画布布局模式。
    lv_display_t* mirror_disp;   ///< 已安装 framebuffer 复制的显示对象。
    app_stereo_render_eye_cb_t render_eye_cb; ///< 右眼独立渲染回调。
    bool mirror_inv_in_progress; ///< 正在追加镜像脏区，防止递归反向扩展。
} app_stereo_state_t;

static app_stereo_state_t g_app_stereo = {
    .output_mode = APP_STEREO_OUTPUT_VERTICAL,
};

/**
 * @brief 获取指定眼位在输出画布中的完整区域。
 * @param[in] eye 目标眼位。
 * @param[out] area 输出眼位区域。
 * @return 无返回值。
 */
static void app_stereo_get_eye_area(app_stereo_eye_t eye, lv_area_t* area) {
    int32_t origin_x = 0;
    int32_t origin_y = 0;

    if (area == NULL) {
        return;
    }

    app_stereo_get_eye_origin(eye, &origin_x, &origin_y);
    lv_area_set(area,
                origin_x,
                origin_y,
                origin_x + app_stereo_get_eye_frame_width() - 1,
                origin_y + app_stereo_get_eye_frame_height() - 1);
}

/**
 * @brief 计算两个区域的交集。
 * @param[out] result 输出交集区域。
 * @param[in] first 第一个区域。
 * @param[in] second 第二个区域。
 * @return `true` 表示存在交集。
 */
static bool app_stereo_area_intersect(lv_area_t* result, const lv_area_t* first, const lv_area_t* second) {
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
 * @brief 判断偏移值是否已经在数组中。
 * @param[in] offsets 偏移值数组。
 * @param[in] count 有效元素数量。
 * @param[in] value 待检查偏移值。
 * @return `true` 表示偏移值已存在。
 */
static bool app_stereo_offset_exists(const int32_t* offsets, size_t count, int32_t value) {
    if (offsets == NULL) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if (offsets[i] == value) {
            return true;
        }
    }

    return false;
}

/**
 * @brief 将一个眼位的局部脏区映射到另一个眼位。
 * @param[in] source 来源眼位内的脏区。
 * @param[in] from 来源眼位。
 * @param[in] to 目标眼位。
 * @param[in] extra_x 目标眼位相对来源眼位的额外 X 轴视差。
 * @param[out] mirrored 输出映射后的区域。
 * @return 无返回值。
 */
static void app_stereo_mirror_eye_area(const lv_area_t* source,
                                       app_stereo_eye_t from,
                                       app_stereo_eye_t to,
                                       int32_t extra_x,
                                       lv_area_t* mirrored) {
    int32_t from_x = 0;
    int32_t from_y = 0;
    int32_t to_x = 0;
    int32_t to_y = 0;

    if (source == NULL || mirrored == NULL) {
        return;
    }

    app_stereo_get_eye_origin(from, &from_x, &from_y);
    app_stereo_get_eye_origin(to, &to_x, &to_y);
    *mirrored = *source;
    lv_area_move(mirrored, to_x - from_x + extra_x, to_y - from_y);
}

/**
 * @brief 将来源眼位的脏区按普通层和视差层追加为目标眼位脏区。
 * @param[in] disp LVGL display。
 * @param[in] source 来源眼位内的脏区交集。
 * @param[in] from 来源眼位。
 * @param[in] to 目标眼位。
 * @return 无返回值。
 */
static void app_stereo_invalidate_mirrored_eye_areas(lv_display_t* disp,
                                                     const lv_area_t* source,
                                                     app_stereo_eye_t from,
                                                     app_stereo_eye_t to) {
    int32_t offsets[3] = { 0 };
    size_t offset_count = 0;
    int32_t app_float_shift_x = app_stereo_node_pos_x_trans(to, FLOATAIR_APP_FLOAT_DISTANCE, 0) -
                                app_stereo_node_pos_x_trans(from, FLOATAIR_APP_FLOAT_DISTANCE, 0);
    int32_t popup_shift_x = app_stereo_node_pos_x_trans(to, FLOATAIR_POPUP_FLOAT_DISTANCE, 0) -
                            app_stereo_node_pos_x_trans(from, FLOATAIR_POPUP_FLOAT_DISTANCE, 0);
    lv_area_t target_eye_area;

    if (disp == NULL || source == NULL) {
        return;
    }

    offsets[offset_count++] = 0;
    if (!app_stereo_offset_exists(offsets, offset_count, app_float_shift_x)) {
        offsets[offset_count++] = app_float_shift_x;
    }
    if (!app_stereo_offset_exists(offsets, offset_count, popup_shift_x)) {
        offsets[offset_count++] = popup_shift_x;
    }

    app_stereo_get_eye_area(to, &target_eye_area);
    for (size_t i = 0; i < offset_count; i++) {
        lv_area_t mirrored;
        lv_area_t clipped;

        app_stereo_mirror_eye_area(source, from, to, offsets[i], &mirrored);
        if (app_stereo_area_intersect(&clipped, &mirrored, &target_eye_area)) {
            g_app_stereo.mirror_inv_in_progress = true;
            lv_inv_area(disp, &clipped);
            g_app_stereo.mirror_inv_in_progress = false;
        }
    }
}

/**
 * @brief 为单眼脏区追加镜像眼相关脏区。
 * @param[in] e LVGL display 事件。
 * @return 无返回值。
 */
static void app_stereo_invalidate_area_event_cb(lv_event_t* e) {
    lv_display_t* disp = (lv_display_t*)lv_event_get_current_target(e);
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    lv_area_t left_area;
    lv_area_t right_area;
    lv_area_t intersection;

    if (!app_stereo_is_enabled() ||
        disp != g_app_stereo.mirror_disp ||
        area == NULL) {
        return;
    }

    if (disp->rendering_in_progress) {
        return;
    }

    if (g_app_stereo.mirror_inv_in_progress) {
        return;
    }

    app_stereo_get_eye_area(APP_STEREO_EYE_LEFT, &left_area);
    app_stereo_get_eye_area(APP_STEREO_EYE_RIGHT, &right_area);

    if (app_stereo_area_intersect(&intersection, area, &left_area)) {
        app_stereo_invalidate_mirrored_eye_areas(disp, &intersection, APP_STEREO_EYE_LEFT, APP_STEREO_EYE_RIGHT);
    }

    if (app_stereo_area_intersect(&intersection, area, &right_area)) {
        app_stereo_invalidate_mirrored_eye_areas(disp, &intersection, APP_STEREO_EYE_RIGHT, APP_STEREO_EYE_LEFT);
    }
}

/**
 * @brief 在完整 framebuffer 中将左眼业务区域复制到右眼业务区域。
 * @param[in] disp LVGL display。
 * @param[in,out] buf 当前活动 draw buffer。
 * @return 无返回值。
 */
static void app_stereo_duplicate_full_frame(lv_display_t* disp, lv_draw_buf_t* buf) {
    int32_t eye_w = app_stereo_get_eye_frame_width();
    int32_t eye_h = app_stereo_get_eye_frame_height();
    int32_t left_x = 0;
    int32_t left_y = 0;
    int32_t right_x = 0;
    int32_t right_y = 0;
    uint32_t px_size = 0;
    uint32_t stride = 0;
    size_t left_last = 0;
    size_t right_last = 0;

    if (!app_stereo_is_enabled() || disp == NULL || buf == NULL || buf->data == NULL) {
        return;
    }

    if ((int32_t)buf->header.w != app_stereo_get_output_width() ||
        (int32_t)buf->header.h != app_stereo_get_output_height()) {
        return;
    }

    stride = buf->header.stride;
    if (stride == 0u) {
        return;
    }

    px_size = lv_color_format_get_size((lv_color_format_t)buf->header.cf);
    if (px_size == 0u) {
        return;
    }

    app_stereo_get_eye_origin(APP_STEREO_EYE_LEFT, &left_x, &left_y);
    app_stereo_get_eye_origin(APP_STEREO_EYE_RIGHT, &right_x, &right_y);
    left_last = ((size_t)left_y + (size_t)eye_h - 1u) * stride + ((size_t)left_x + (size_t)eye_w) * px_size;
    right_last = ((size_t)right_y + (size_t)eye_h - 1u) * stride + ((size_t)right_x + (size_t)eye_w) * px_size;
    if (buf->data_size < left_last || buf->data_size < right_last) {
        return;
    }

    for (int32_t y = 0; y < eye_h; y++) {
        uint8_t* src = buf->data + ((size_t)left_y + (size_t)y) * stride + (size_t)left_x * px_size;
        uint8_t* dst = buf->data + ((size_t)right_y + (size_t)y) * stride + (size_t)right_x * px_size;

        memmove(dst, src, (size_t)eye_w * px_size);
    }
}

/**
 * @brief app framework 双眼 framebuffer 复制事件回调。
 * @param[in] e LVGL display 事件。
 * @return 无返回值。
 */
static void app_stereo_flush_start_event_cb(lv_event_t* e) {
    lv_display_t* disp = (lv_display_t*)lv_event_get_current_target(e);
    const lv_area_t* flush_area = (const lv_area_t*)lv_event_get_param(e);
    lv_draw_buf_t* buf = NULL;

    if (disp != g_app_stereo.mirror_disp) {
        return;
    }

    buf = lv_display_get_buf_active(disp);
    if (g_app_stereo.render_eye_cb != NULL &&
        g_app_stereo.render_eye_cb(disp, buf, flush_area, APP_STEREO_EYE_RIGHT)) {
        return;
    }

    app_stereo_duplicate_full_frame(disp, buf);
}

/**
 * @brief 清理已删除 display 的双眼 framebuffer 复制状态。
 * @param[in] e LVGL display 事件。
 * @return 无返回值。
 */
static void app_stereo_display_delete_event_cb(lv_event_t* e) {
    lv_display_t* disp = (lv_display_t*)lv_event_get_current_target(e);

    if (disp == g_app_stereo.mirror_disp) {
        g_app_stereo.mirror_disp = NULL;
    }
}

void app_stereo_set_render_eye_cb(app_stereo_render_eye_cb_t cb) {
    g_app_stereo.render_eye_cb = cb;
}

bool app_stereo_install_display_mirror(lv_display_t* disp) {
    if (!app_stereo_is_enabled()) {
        return true;
    }

    if (disp == NULL) {
        return false;
    }

    if (g_app_stereo.mirror_disp == disp) {
        return true;
    }

    if (g_app_stereo.mirror_disp != NULL) {
        floatair_warn("app stereo display mirror already installed");
        return false;
    }

    g_app_stereo.mirror_disp = disp;
    lv_display_add_event_cb(disp, app_stereo_invalidate_area_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_add_event_cb(disp, app_stereo_flush_start_event_cb, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(disp, app_stereo_display_delete_event_cb, LV_EVENT_DELETE, NULL);
    floatair_info("app stereo display mirror installed: disp=%p", disp);
    return true;
}

bool app_stereo_is_enabled(void) {
#if !defined(BUILD_NATIVE)
    return SYSTEM_LCD_STEREO_ENABLED != 0u;
#else
    return SYSTEM_LCD_STEREO_ENABLED != 0u &&
           g_app_stereo.output_mode != APP_STEREO_OUTPUT_SINGLE;
#endif
}

int32_t app_stereo_get_eye_frame_width(void) {
    return (int32_t)SYSTEM_LCD_EYE_FRAME_WIDTH;
}

int32_t app_stereo_get_eye_frame_height(void) {
    return (int32_t)SYSTEM_LCD_EYE_FRAME_HEIGHT;
}

int32_t app_stereo_get_output_width(void) {
    if (!app_stereo_is_enabled()) {
        return app_stereo_get_eye_frame_width();
    }

    if (g_app_stereo.output_mode == APP_STEREO_OUTPUT_HORIZONTAL) {
        return app_stereo_get_eye_frame_width() * 2;
    }

    return app_stereo_get_eye_frame_width();
}

int32_t app_stereo_get_output_height(void) {
    if (!app_stereo_is_enabled()) {
        return app_stereo_get_eye_frame_height();
    }

    if (g_app_stereo.output_mode == APP_STEREO_OUTPUT_HORIZONTAL) {
        return app_stereo_get_eye_frame_height();
    }

    return app_stereo_get_eye_frame_height() * 2;
}

bool app_stereo_set_output_mode(app_stereo_output_mode_t mode) {
    if (mode != APP_STEREO_OUTPUT_VERTICAL &&
        mode != APP_STEREO_OUTPUT_HORIZONTAL &&
        mode != APP_STEREO_OUTPUT_SINGLE) {
        return false;
    }

#if !defined(BUILD_NATIVE)
    if (mode != APP_STEREO_OUTPUT_VERTICAL) {
        floatair_warn("app stereo output mode is vertical-only on ARM");
        return false;
    }
#endif

    if (g_app_stereo.mirror_disp != NULL) {
        floatair_warn("app stereo output mode must be set before display mirror install");
        return false;
    }

    g_app_stereo.output_mode = mode;
    floatair_info("app stereo output mode=%d", (int)mode);
    return true;
}

app_stereo_output_mode_t app_stereo_get_output_mode(void) {
#if !defined(BUILD_NATIVE)
    return APP_STEREO_OUTPUT_VERTICAL;
#else
    return g_app_stereo.output_mode;
#endif
}

void app_stereo_get_eye_origin(app_stereo_eye_t eye, int32_t* x, int32_t* y) {
    int32_t origin_x = 0;
    int32_t origin_y = 0;

    if (app_stereo_is_enabled() && eye == APP_STEREO_EYE_RIGHT) {
        if (g_app_stereo.output_mode == APP_STEREO_OUTPUT_HORIZONTAL) {
            origin_x = app_stereo_get_eye_frame_width();
        } else {
            origin_y = app_stereo_get_eye_frame_height();
        }
    }

    if (x != NULL) {
        *x = origin_x;
    }
    if (y != NULL) {
        *y = origin_y;
    }
}

int32_t app_stereo_node_pos_x_trans(app_stereo_eye_t eye, int32_t delta_z, int32_t x) {
    if (!app_stereo_is_enabled()) {
        return x;
    }

    if (eye == APP_STEREO_EYE_RIGHT) {
        return x + delta_z;
    }

    return x - delta_z;
}
