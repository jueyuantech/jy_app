/**
 * @file app_stereo.h
 * @brief app 框架双眼显示尺寸与偏移计算接口。
 * @author jytek
 * @version 1.0.0
 * @date 2026-05-02
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#pragma once

#include <lvgl/lvgl.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 双眼输出中的眼位标识。
 */
typedef enum {
    APP_STEREO_EYE_LEFT = 0,  ///< 左眼画面。
    APP_STEREO_EYE_RIGHT,     ///< 右眼画面。
} app_stereo_eye_t;

/**
 * @brief 双眼输出画布布局模式。
 */
typedef enum {
    APP_STEREO_OUTPUT_VERTICAL = 0,  ///< 双眼按上下竖向堆叠输出。
    APP_STEREO_OUTPUT_HORIZONTAL,    ///< 双眼按左右横向并排输出。
    APP_STEREO_OUTPUT_SINGLE,        ///< 仅输出单眼业务区域。
} app_stereo_output_mode_t;

/**
 * @brief 双眼输出右眼渲染回调。
 */
typedef bool (*app_stereo_render_eye_cb_t)(lv_display_t* disp,
                                           lv_draw_buf_t* buf,
                                           const lv_area_t* flush_area,
                                           app_stereo_eye_t eye);

/**
 * @brief 设置双眼输出画布布局模式。
 * @param[in] mode 输出布局模式。
 * @return `true` 表示设置成功，`false` 表示显示对象已安装或参数无效。
 */
bool app_stereo_set_output_mode(app_stereo_output_mode_t mode);

/**
 * @brief 获取当前双眼输出画布布局模式。
 * @return 返回当前输出布局模式。
 */
app_stereo_output_mode_t app_stereo_get_output_mode(void);

/**
 * @brief 设置右眼渲染回调。
 * @param[in] cb 右眼渲染回调；传 `NULL` 时清除回调。
 * @return 无返回值。
 */
void app_stereo_set_render_eye_cb(app_stereo_render_eye_cb_t cb);

/**
 * @brief 为 LVGL display 安装双眼 framebuffer 复制事件。
 * @param[in] disp 需要安装事件的 LVGL display。
 * @return `true` 表示安装成功或已经安装，`false` 表示安装失败。
 */
bool app_stereo_install_display_mirror(lv_display_t* disp);

/**
 * @brief 查询当前是否启用双眼堆叠输出。
 * @return `true` 表示启用双眼输出，`false` 表示单眼输出。
 */
bool app_stereo_is_enabled(void);

/**
 * @brief 获取单眼业务切分宽度。
 * @return 返回单眼业务区域宽度。
 */
int32_t app_stereo_get_eye_frame_width(void);

/**
 * @brief 获取单眼业务切分高度。
 * @return 返回单眼业务区域高度。
 */
int32_t app_stereo_get_eye_frame_height(void);

/**
 * @brief 获取双眼输出画布宽度。
 * @return 返回 LVGL 屏幕输出宽度。
 */
int32_t app_stereo_get_output_width(void);

/**
 * @brief 获取双眼输出画布高度。
 * @return 返回 LVGL 屏幕输出高度。
 */
int32_t app_stereo_get_output_height(void);

/**
 * @brief 获取指定眼位在输出画布中的业务区域起点。
 * @param[in] eye 目标眼位。
 * @param[out] x 输出眼区起点 X，可为 `NULL`。
 * @param[out] y 输出眼区起点 Y，可为 `NULL`。
 * @return 无返回值。
 */
void app_stereo_get_eye_origin(app_stereo_eye_t eye, int32_t* x, int32_t* y);

/**
 * @brief 计算指定眼位下浮层节点的 X 坐标。
 * @param[in] eye 目标眼位。
 * @param[in] delta_z 节点相对根层的浮动距离。
 * @param[in] x 原始 X 坐标。
 * @return 返回按眼位应用固定视差后的 X 坐标。
 */
int32_t app_stereo_node_pos_x_trans(app_stereo_eye_t eye, int32_t delta_z, int32_t x);

#ifdef __cplusplus
}
#endif
