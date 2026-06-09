/**
 * @file system_runtime_types.h
 * @brief 系统运行时公共类型声明
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#pragma once

#include "app_def.h"
#include "elf_common.h"
#include "floatair_def.h"

#include <lvgl/lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_TOUCH_EVENT_CLICKED 0x01       ///< 单击触摸事件
#define SYSTEM_TOUCH_EVENT_DCLICKED 0x02      ///< 双击触摸事件
#define SYSTEM_TOUCH_EVENT_LONG_PRESSED 0x03  ///< 长按触摸事件
#define SYSTEM_TOUCH_EVENT_GESTURE_UP 0x04    ///< 上滑手势事件
#define SYSTEM_TOUCH_EVENT_GESTURE_DOWN 0x05  ///< 下滑手势事件
#define SYSTEM_TOUCH_EVENT_GESTURE_LEFT 0x06  ///< 左滑手势事件
#define SYSTEM_TOUCH_EVENT_GESTURE_RIGHT 0x07 ///< 右滑手势事件

/**
 * @brief 应用字体设置（字重、字间距、行间距）。
 */
typedef struct {
    uint32_t weight;    ///< 字重
    uint32_t wordSpace; ///< 字间距
    uint32_t rowSpace;  ///< 行间距
} app_font_info_t;

/**
 * @brief 系统 LCD 显示区域。
 */
#define SYSTEM_LCD_UI_WIDTH             (540u) ///< 业务可见 UI 区域宽度。
#define SYSTEM_LCD_UI_HEIGHT            (440u) ///< 单眼业务可见 UI 区域高度。
#define SYSTEM_LCD_EYE_FRAME_WIDTH      (SYSTEM_LCD_UI_WIDTH) ///< 单眼业务切分区域宽度。
#define SYSTEM_LCD_EYE_FRAME_HEIGHT     (SYSTEM_LCD_UI_HEIGHT) ///< 单眼业务切分区域高度。
#define SYSTEM_LCD_UI_X_BEGIN           (0u)    ///< UI 在业务区域内的起始 X。
#define SYSTEM_LCD_UI_Y_BEGIN           (0u)    ///< UI 在业务区域内的起始 Y。
#define SYSTEM_LCD_STEREO_ENABLED       (1u)   ///< 是否启用上下堆叠双眼输出。
#define SYSTEM_LCD_STEREO_OUTPUT_WIDTH  (SYSTEM_LCD_EYE_FRAME_WIDTH) ///< 双眼输出画布宽度。
#define SYSTEM_LCD_STEREO_OUTPUT_HEIGHT (SYSTEM_LCD_EYE_FRAME_HEIGHT * 2u) ///< 双眼输出画布高度。

typedef struct {
    uint32_t ui_x_begin; ///< UI 起始 X
    uint32_t ui_y_begin; ///< UI 起始 Y
    uint32_t ui_width;   ///< UI 宽度
    uint32_t ui_height;  ///< UI 高度
} system_lcd_t;

/**
 * @brief 抬头/低头手势配置。
 */
typedef struct {
    bool up_enabled;   ///< 抬头手势开关
    bool down_enabled; ///< 低头手势开关
    int32_t up_deg;    ///< 抬头触发阈值，单位为度
    int32_t down_deg;  ///< 低头触发阈值，单位为度
    int32_t base_deg;  ///< 基准角度，单位为度
} system_head_gesture_config_t;

/**
 * @brief 首页应用单元信息。
 */
typedef struct {
    const char* name;       ///< app 名称
    const char* bigicon;    ///< 大图标资源
    const char* smallicon;  ///< 小图标资源
    const char* icontext;   ///< 展示文案路径
} app_home_unit_t;

/**
 * @brief 全局 LCD 显示区域配置。
 */
extern system_lcd_t config_lcd;
/**
 * @brief 全局工厂区数据缓存。
 */
extern jyt_section_data_t g_section_data;
/**
 * @brief 全局蓝牙信息缓存。
 */
extern bt_info g_bt_info;

#ifdef __cplusplus
}
#endif
