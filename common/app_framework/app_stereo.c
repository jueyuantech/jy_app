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

#include "app_lcd.h"
#include "app_def.h"
#include "floatair_dbg.h"
#include "lvgl/src/core/lv_refr_private.h"
#include "lvgl/src/display/lv_display_private.h"
#include "lvgl/src/draw/lv_draw_buf.h"
#include "system/system_runtime_types.h"

#define APP_STEREO_INV_AREA_COMPACT_THRESHOLD (LV_INV_BUF_SIZE - 4u) ///< LVGL 脏区接近上限时提前压缩源脏区。
#define APP_STEREO_AREA_NEAR_GAP_MAX 2u ///< 允许近邻小脏区合并的最大间隔像素。
#define APP_STEREO_AREA_NEAR_EXTRA_MAX 256u ///< 允许局部小脏区合并额外增加的最大面积。
#define APP_STEREO_AREA_EXTRA_RATIO_DIV 16u ///< 大脏区边缘扩展合并时允许增加的相对面积比例分母。

typedef struct {
    app_stereo_output_mode_t output_mode; ///< 当前双眼输出画布布局模式。
    lv_display_t* mirror_disp;   ///< 已安装 framebuffer 复制的显示对象。
    app_stereo_render_eye_cb_t render_eye_cb; ///< 右眼独立渲染回调。
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
 * @brief 判断一个区域是否完整包含另一个区域。
 * @param[in] holder 外层区域。
 * @param[in] area 待检查区域。
 * @return `true` 表示 area 完整位于 holder 内部。
 */
static bool app_stereo_area_contains(const lv_area_t* holder, const lv_area_t* area) {
    if (holder == NULL || area == NULL) {
        return false;
    }

    return holder->x1 <= area->x1 &&
           holder->y1 <= area->y1 &&
           holder->x2 >= area->x2 &&
           holder->y2 >= area->y2;
}

/**
 * @brief 合并两个区域的外接矩形。
 * @param[out] result 输出外接矩形。
 * @param[in] first 第一个区域。
 * @param[in] second 第二个区域。
 * @return 无返回值。
 */
static void app_stereo_area_join(lv_area_t* result, const lv_area_t* first, const lv_area_t* second) {
    if (result == NULL || first == NULL || second == NULL) {
        return;
    }

    result->x1 = LV_MIN(first->x1, second->x1);
    result->y1 = LV_MIN(first->y1, second->y1);
    result->x2 = LV_MAX(first->x2, second->x2);
    result->y2 = LV_MAX(first->y2, second->y2);
}

/**
 * @brief 计算两个区域之间的轴向间隔。
 * @param[in] first_start 第一个区域起点。
 * @param[in] first_end 第一个区域终点。
 * @param[in] second_start 第二个区域起点。
 * @param[in] second_end 第二个区域终点。
 * @return 返回两个区域之间的空白像素数量，重叠时返回 0。
 */
static uint32_t app_stereo_axis_gap(int32_t first_start, int32_t first_end, int32_t second_start, int32_t second_end) {
    if (first_end < second_start) {
        return (uint32_t)(second_start - first_end - 1);
    }

    if (second_end < first_start) {
        return (uint32_t)(first_start - second_end - 1);
    }

    return 0;
}

/**
 * @brief 判断两个未相交区域是否足够接近且值得合并。
 * @param[in] first 第一个区域。
 * @param[in] second 第二个区域。
 * @param[in] joined 两个区域的外接矩形。
 * @return `true` 表示近邻合并只会带来很小额外面积。
 */
static bool app_stereo_area_should_merge_near(const lv_area_t* first, const lv_area_t* second, const lv_area_t* joined) {
    uint32_t gap_x;
    uint32_t gap_y;
    uint32_t first_size;
    uint32_t second_size;
    uint32_t joined_size;

    if (first == NULL || second == NULL || joined == NULL) {
        return false;
    }

    gap_x = app_stereo_axis_gap(first->x1, first->x2, second->x1, second->x2);
    gap_y = app_stereo_axis_gap(first->y1, first->y2, second->y1, second->y2);
    if ((gap_x > APP_STEREO_AREA_NEAR_GAP_MAX || gap_y != 0u) &&
        (gap_y > APP_STEREO_AREA_NEAR_GAP_MAX || gap_x != 0u)) {
        return false;
    }

    first_size = lv_area_get_size(first);
    second_size = lv_area_get_size(second);
    joined_size = lv_area_get_size(joined);
    uint32_t base_size = first_size + second_size;
    uint32_t allowed_extra = LV_MAX(APP_STEREO_AREA_NEAR_EXTRA_MAX, base_size / APP_STEREO_AREA_EXTRA_RATIO_DIV);
    return joined_size <= base_size + allowed_extra;
}

/**
 * @brief 判断两个脏区是否值得提前合并。
 * @param[in] first 第一个区域。
 * @param[in] second 第二个区域。
 * @return `true` 表示合并后渲染面积不大于分开渲染。
 */
static bool app_stereo_area_should_merge(const lv_area_t* first, const lv_area_t* second) {
    lv_area_t intersection;
    lv_area_t joined;

    if (first == NULL || second == NULL) {
        return false;
    }

    if (app_stereo_area_contains(first, second) || app_stereo_area_contains(second, first)) {
        return true;
    }

    app_stereo_area_join(&joined, first, second);
    if (!app_stereo_area_intersect(&intersection, first, second)) {
        return app_stereo_area_should_merge_near(first, second, &joined);
    }

    uint32_t base_size = lv_area_get_size(first) + lv_area_get_size(second);
    uint32_t allowed_extra = LV_MAX(APP_STEREO_AREA_NEAR_EXTRA_MAX, base_size / APP_STEREO_AREA_EXTRA_RATIO_DIV);
    return lv_area_get_size(&joined) <= base_size + allowed_extra;
}

/**
 * @brief 判断两个区域是否至少落在同一个眼位区域内。
 * @param[in] first 第一个区域。
 * @param[in] second 第二个区域。
 * @return `true` 表示两个区域同属左眼或同属右眼。
 */
static bool app_stereo_area_shares_eye(const lv_area_t* first, const lv_area_t* second) {
    lv_area_t left_area;
    lv_area_t right_area;
    lv_area_t intersection;
    bool first_on_left = false;
    bool first_on_right = false;
    bool second_on_left = false;
    bool second_on_right = false;

    if (first == NULL || second == NULL) {
        return false;
    }

    app_stereo_get_eye_area(APP_STEREO_EYE_LEFT, &left_area);
    app_stereo_get_eye_area(APP_STEREO_EYE_RIGHT, &right_area);
    first_on_left = app_stereo_area_intersect(&intersection, first, &left_area);
    first_on_right = app_stereo_area_intersect(&intersection, first, &right_area);
    second_on_left = app_stereo_area_intersect(&intersection, second, &left_area);
    second_on_right = app_stereo_area_intersect(&intersection, second, &right_area);
    return (first_on_left && second_on_left) || (first_on_right && second_on_right);
}

/**
 * @brief 查找合入指定脏区后面积最小的已有脏区。
 * @param[in] disp LVGL display。
 * @param[in] area 待合入区域。
 * @param[in] same_eye_only 是否只允许同眼区域。
 * @param[out] index 输出最佳下标。
 * @return `true` 表示找到可合并区域。
 */
static bool app_stereo_find_best_join_index(lv_display_t* disp, const lv_area_t* area, bool same_eye_only, uint16_t* index) {
    uint32_t best_join_size = UINT32_MAX;
    uint16_t best_join_index = 0;
    bool found = false;

    if (disp == NULL || area == NULL || index == NULL) {
        return false;
    }

    for (uint16_t i = 0; i < disp->inv_p; i++) {
        lv_area_t joined;
        uint32_t joined_size = 0;

        if (same_eye_only && !app_stereo_area_shares_eye(&disp->inv_areas[i], area)) {
            continue;
        }

        app_stereo_area_join(&joined, &disp->inv_areas[i], area);
        joined_size = lv_area_get_size(&joined);
        if (!found || joined_size < best_join_size) {
            best_join_size = joined_size;
            best_join_index = i;
            found = true;
        }
    }

    if (found) {
        *index = best_join_index;
    }
    return found;
}

/**
 * @brief 在 LVGL 槽位满前压缩源脏区，避免 lv_inv_area 走整屏兜底。
 * @param[in,out] disp LVGL display。
 * @param[in] area 当前正在进入 LVGL 的脏区。
 * @return 无返回值。
 */
static void app_stereo_compact_incoming_inv_area(lv_display_t* disp, const lv_area_t* area) {
    uint16_t best_join_index = 0;

    if (disp == NULL || area == NULL || disp->inv_p < APP_STEREO_INV_AREA_COMPACT_THRESHOLD) {
        return;
    }

    for (uint16_t i = 0; i < disp->inv_p; i++) {
        if (app_stereo_area_contains(&disp->inv_areas[i], area)) {
            return;
        }

        if (app_stereo_area_should_merge(&disp->inv_areas[i], area)) {
            app_stereo_area_join(&disp->inv_areas[i], &disp->inv_areas[i], area);
            disp->inv_area_joined[i] = 0;
            return;
        }
    }

    if (!app_stereo_find_best_join_index(disp, area, true, &best_join_index) &&
        !app_stereo_find_best_join_index(disp, area, false, &best_join_index)) {
        return;
    }

    app_stereo_area_join(&disp->inv_areas[best_join_index], &disp->inv_areas[best_join_index], area);
    disp->inv_area_joined[best_join_index] = 0;
}

/**
 * @brief 在渲染前按左右眼收敛 LVGL 脏区，避免 partial 渲染多区域重复遍历对象。
 * @param[in,out] disp LVGL display。
 * @return 无返回值。
 */
static void app_stereo_compact_display_inv_areas(lv_display_t* disp) {
    lv_area_t compacted_areas[APP_STEREO_EYE_RIGHT + 1];
    app_stereo_eye_t eyes[] = {APP_STEREO_EYE_LEFT, APP_STEREO_EYE_RIGHT};
    uint16_t compacted_count = 0;

    if (disp == NULL || disp->inv_p == 0u) {
        return;
    }

    for (size_t eye_index = 0; eye_index < sizeof(eyes) / sizeof(eyes[0]); eye_index++) {
        lv_area_t eye_area;
        lv_area_t eye_joined;
        bool has_area = false;

        app_stereo_get_eye_area(eyes[eye_index], &eye_area);
        for (uint16_t i = 0; i < disp->inv_p; i++) {
            lv_area_t clipped;

            if (!app_stereo_area_intersect(&clipped, &disp->inv_areas[i], &eye_area)) {
                continue;
            }

            if (!has_area) {
                eye_joined = clipped;
                has_area = true;
                continue;
            }
            app_stereo_area_join(&eye_joined, &eye_joined, &clipped);
        }

        if (has_area) {
            compacted_areas[compacted_count] = eye_joined;
            compacted_count++;
        }
    }

    if (compacted_count == 0u) {
        return;
    }

    for (uint16_t i = 0; i < compacted_count; i++) {
        disp->inv_areas[i] = compacted_areas[i];
        disp->inv_area_joined[i] = 0;
    }
    disp->inv_p = compacted_count;
}

/**
 * @brief 将一个区域直接合入 LVGL 当前脏区队列，避免触发 LVGL 槽位满后的整屏兜底。
 * @param[in,out] disp LVGL display。
 * @param[in] area 待合入区域。
 * @return 无返回值。
 */
static void app_stereo_insert_display_inv_area(lv_display_t* disp, const lv_area_t* area) {
    uint32_t best_join_size = UINT32_MAX;
    uint16_t best_join_index = 0;

    if (disp == NULL || area == NULL) {
        return;
    }

    for (uint16_t i = 0; i < disp->inv_p; i++) {
        lv_area_t joined;

        if (app_stereo_area_contains(&disp->inv_areas[i], area)) {
            return;
        }

        if (app_stereo_area_should_merge(&disp->inv_areas[i], area)) {
            app_stereo_area_join(&disp->inv_areas[i], &disp->inv_areas[i], area);
            disp->inv_area_joined[i] = 0;
            return;
        }

        app_stereo_area_join(&joined, &disp->inv_areas[i], area);
        uint32_t joined_size = lv_area_get_size(&joined);
        if (joined_size < best_join_size) {
            best_join_size = joined_size;
            best_join_index = i;
        }
    }

    if (disp->inv_p < LV_INV_BUF_SIZE) {
        disp->inv_areas[disp->inv_p] = *area;
        disp->inv_area_joined[disp->inv_p] = 0;
        disp->inv_p++;
        return;
    }

    app_stereo_area_join(&disp->inv_areas[best_join_index], &disp->inv_areas[best_join_index], area);
    disp->inv_area_joined[best_join_index] = 0;
}

/**
 * @brief 获取眼位名称。
 * @param[in] eye 目标眼位。
 * @return 返回用于日志输出的眼位名称。
 */
static const char* app_stereo_eye_name(app_stereo_eye_t eye) {
    return eye == APP_STEREO_EYE_LEFT ? "left" : "right";
}

/**
 * @brief 按指定眼位打印当前 LVGL 脏区列表。
 * @param[in] disp LVGL display。
 * @param[in] stage 日志阶段名称。
 * @param[in] eye 目标眼位。
 * @param[in] active_only 是否只打印未被合并标记的最终区域。
 * @return 无返回值。
 */
static void app_stereo_log_eye_inv_areas(lv_display_t* disp,
                                         const char* stage,
                                         app_stereo_eye_t eye,
                                         bool active_only) {
    lv_area_t eye_area;
    int32_t eye_x = 0;
    int32_t eye_y = 0;
    uint16_t active_count = 0;

    if (disp == NULL || stage == NULL || disp->inv_p == 0u) {
        return;
    }

    app_stereo_get_eye_area(eye, &eye_area);
    app_stereo_get_eye_origin(eye, &eye_x, &eye_y);
    for (uint16_t i = 0; i < disp->inv_p; i++) {
        lv_area_t clipped;

        if (active_only && disp->inv_area_joined[i] != 0) {
            continue;
        }

        if (app_stereo_area_intersect(&clipped, &disp->inv_areas[i], &eye_area)) {
            active_count++;
        }
    }

    floatair_info("app stereo inv %s %s count=%u raw=%lu",
                  stage,
                  app_stereo_eye_name(eye),
                  (unsigned)active_count,
                  (unsigned long)disp->inv_p);
    for (uint16_t i = 0; i < disp->inv_p; i++) {
        const lv_area_t* area = &disp->inv_areas[i];
        lv_area_t clipped;
        lv_area_t local;

        if (active_only && disp->inv_area_joined[i] != 0) {
            continue;
        }

        if (!app_stereo_area_intersect(&clipped, area, &eye_area)) {
            continue;
        }

        local = clipped;
        lv_area_move(&local, -eye_x, -eye_y);
        floatair_info("app stereo inv %s %s[%u]=abs(%d,%d)-(%d,%d) local(%d,%d)-(%d,%d) size=%lu joined=%u",
                      stage,
                      app_stereo_eye_name(eye),
                      (unsigned)i,
                      (int)clipped.x1,
                      (int)clipped.y1,
                      (int)clipped.x2,
                      (int)clipped.y2,
                      (int)local.x1,
                      (int)local.y1,
                      (int)local.x2,
                      (int)local.y2,
                      (unsigned long)lv_area_get_size(&clipped),
                      (unsigned)disp->inv_area_joined[i]);
    }
}

/**
 * @brief 分左右眼打印当前 LVGL 脏区列表。
 * @param[in] disp LVGL display。
 * @param[in] stage 日志阶段名称。
 * @param[in] active_only 是否只打印未被合并标记的最终区域。
 * @return 无返回值。
 */
static void app_stereo_log_inv_areas(lv_display_t* disp, const char* stage, bool active_only) {
    if (disp == NULL) {
        return;
    }

    app_stereo_log_eye_inv_areas(disp, stage, APP_STEREO_EYE_LEFT, active_only);
    app_stereo_log_eye_inv_areas(disp, stage, APP_STEREO_EYE_RIGHT, active_only);
}

/**
 * @brief 在 LVGL 渲染合并前统一压缩本帧左右眼脏区。
 * @param[in] e LVGL display 事件。
 * @return 无返回值。
 */
static void app_stereo_refr_start_event_cb(lv_event_t* e) {
    lv_display_t* disp = (lv_display_t*)lv_event_get_current_target(e);

    if (disp != g_app_stereo.mirror_disp) {
        return;
    }

    if (floatair_lcd_is_off()) {
        return;
    }

    if (disp->render_mode == LV_DISPLAY_RENDER_MODE_FULL) {
        return;
    }

    app_stereo_log_inv_areas(disp, "small", false);
    app_stereo_compact_display_inv_areas(disp);
}

/**
 * @brief 在 LVGL 最终合并脏区后打印实际渲染区域。
 * @param[in] e LVGL display 事件。
 * @return 无返回值。
 */
static void app_stereo_render_start_event_cb(lv_event_t* e) {
    lv_display_t* disp = (lv_display_t*)lv_event_get_current_target(e);

    if (disp != g_app_stereo.mirror_disp) {
        return;
    }

    if (floatair_lcd_is_off()) {
        return;
    }

    app_stereo_log_inv_areas(disp, "merged", true);
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
            app_stereo_insert_display_inv_area(disp, &clipped);
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

    if (floatair_lcd_is_off()) {
        return;
    }

    if (disp->rendering_in_progress) {
        return;
    }

    app_stereo_compact_incoming_inv_area(disp, area);

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
 * @brief 判断指定 flush 区域是否覆盖目标眼位。
 * @param[in] flush_area 本次 flush 区域，`NULL` 表示未知区域。
 * @param[in] eye 目标眼位。
 * @return `true` 表示覆盖目标眼位或区域未知。
 */
static bool app_stereo_flush_area_touches_eye(const lv_area_t* flush_area, app_stereo_eye_t eye) {
    lv_area_t eye_area;
    lv_area_t intersection;

    if (flush_area == NULL) {
        return true;
    }

    app_stereo_get_eye_area(eye, &eye_area);
    return app_stereo_area_intersect(&intersection, flush_area, &eye_area);
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

    if (floatair_lcd_is_off()) {
        return;
    }

    buf = lv_display_get_buf_active(disp);
    if (g_app_stereo.render_eye_cb != NULL &&
        g_app_stereo.render_eye_cb(disp, buf, flush_area, APP_STEREO_EYE_RIGHT)) {
        return;
    }

    if (g_app_stereo.render_eye_cb != NULL &&
        !app_stereo_flush_area_touches_eye(flush_area, APP_STEREO_EYE_RIGHT)) {
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
    lv_display_add_event_cb(disp, app_stereo_refr_start_event_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, app_stereo_render_start_event_cb, LV_EVENT_RENDER_START, NULL);
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
