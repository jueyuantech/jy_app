/**
 * @file lv_nuttx_lcd.h
 * @brief NuttX LCD display driver integration for LVGL.
 */

#ifndef LV_NUTTX_LCD_H
#define LV_NUTTX_LCD_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "../../display/lv_display.h"

#if LV_USE_NUTTX

#if LV_USE_NUTTX_LCD

/*********************
 *      DEFINES
 *********************/

#ifndef LV_NUTTX_LCD_FLUSH_PARTIAL_AREA
#define LV_NUTTX_LCD_FLUSH_PARTIAL_AREA 0 /**< 是否按 LVGL 当前脏区外接矩形提交 LCD 刷屏区域。 */
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

lv_display_t * lv_nuttx_lcd_create(const char * dev_path);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_NUTTX_LCD */

#endif /* LV_USE_NUTTX*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_NUTTX_LCD_H */
