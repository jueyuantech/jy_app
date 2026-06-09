/**
 * @file system_def.h
 * @brief System constants and dimension definitions
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

/**
 * @brief LVGL UI margin constants
 *
 * Standard margin sizes for page layouts
 */
#define LVGL_UI_MARGIN_2   (2)
#define LVGL_UI_MARGIN_5   (5)
#define LVGL_UI_MARGIN_10  (10)
#define LVGL_UI_MARGIN_15  (15)
#define LVGL_UI_MARGIN_20  (20)
#define LVGL_UI_MARGIN_30  (30)
#define LVGL_UI_MARGIN_40  (40)
#define LVGL_UI_MARGIN_50  (50)
#define LVGL_UI_MARGIN_60  (60)
#define LVGL_UI_MARGIN_70  (70)
#define LVGL_UI_MARGIN_80  (80)
#define LVGL_UI_MARGIN_90  (90)
#define LVGL_UI_MARGIN_100 (100)
#define LVGL_UI_MARGIN_110 (110)
#define LVGL_UI_MARGIN_120 (120)
#define LVGL_UI_MARGIN_130 (130)
#define LVGL_UI_MARGIN_140 (140)
#define LVGL_UI_MARGIN_160 (160)
#define LVGL_UI_MARGIN_180 (180)
#define LVGL_UI_MARGIN_200 (200)
#define LVGL_UI_MARGIN_220 (220)
#define LVGL_UI_MARGIN_240 (240)

/**
 * @brief LVGL icon size constants
 *
 * Unified icon width and height definitions
 */
#define LVGL_UI_ICONW_10 (10)
#define LVGL_UI_ICONH_10 (10)
#define LVGL_UI_ICONW_16 (16)
#define LVGL_UI_ICONH_16 (16)
#define LVGL_UI_ICONW_32 (32)
#define LVGL_UI_ICONH_32 (32)
#define LVGL_UI_ICONW_40 (40)
#define LVGL_UI_ICONH_40 (40)
#define LVGL_UI_ICONW_48 (48)
#define LVGL_UI_ICONH_48 (48)
#define LVGL_UI_ICONW_64 (64)
#define LVGL_UI_ICONH_64 (64)
#define LVGL_UI_ICONW_80 (80)
#define LVGL_UI_ICONH_80 (80)

/**
 * @brief LVGL button size constants
 *
 * Standard button width and height
 */
#define LVGL_UI_BUTTON_H (40)
#define LVGL_UI_BUTTON_W (180)

/**
 * @brief Page overall width constants
 *
 * Overall layout width definitions
 */
#define LVGL_UI_ROAD_W      (240)
#define LVGL_UI_WIDTH_W_460 (460)
#define LVGL_UI_WIDTH_W_480 (480)

/**
 * @brief Base time display area size
 *
 * Coordinates and size of the top time display area
 */
#define PAGE_BASE_TIME_BEGIN_H (80 + 28)
#define PAGE_BASE_TIME_BEGIN_V (433)
#define PAGE_BASE_TIME_WIDTH   (70)
#define PAGE_BASE_TIME_HEIGHT  (34)

/**
 * @brief Power display area size
 *
 * Coordinates and size of the top power display area
 */
#define PAGE_BASE_POWER_BEGIN_H (80 + 399)
#define PAGE_BASE_POWER_BEGIN_V (450)
#define PAGE_BASE_POWER_WIDTH   (53)
#define PAGE_BASE_POWER_HEIGHT  (34)

/**
 * @brief Page base margins and default width
 *
 * Left/right margins and default content width
 */
#define PAGE_BASE_MARGIN_LEFT (28)
#define PAGE_BASE_LEFT_H      (80 + 28)

#define PAGE_BASE_ICON_H         (32)
#define PAGE_BASE_ICON_V         (32)
#define PAGE_BASE_CONTENT_BOTTOM (50)
#ifndef PAGE_BASE_DEFAULT_WDITH
#define PAGE_BASE_DEFAULT_WDITH  (450)
#endif

/**
 * @brief Font-related parameters
 *
 * Basic parameters such as font padding
 */
#define PAGE_BASE_FONT_PAD_H (10)

/**
 * @brief Top indicator area coordinates
 *
 * Coordinates of power and audio source indicators
 */
#define PAGE_BASE_POWERIMG_BEGIN_H    (80 + 360)
#define PAGE_BASE_POWERIMG_BEGIN_V    (434)
#define PAGE_BASE_AUDIOSOURCE_BEGIN_H (80 + 164)
#define PAGE_BASE_AUDIOSOURCE_BEGIN_V (448)

/**
 * @brief Language display area parameters
 *
 * Coordinates and sizes of source/target languages
 */
#define PAGE_BASE_LANGSRC_BEGIN_H (80 + 28)
#define PAGE_BASE_LANGSRC_BEGIN_V (110)
#define PAGE_BASE_LANGSRC_WIDTH   (33)
#define PAGE_BASE_LANGSRC_HEIGHT  (34)

#define PAGE_BASE_LANGDIST_BEGIN_H (80 + 108)
#define PAGE_BASE_LANGDIST_BEGIN_V (122)
#define PAGE_BASE_LANGDIST_WIDTH   (33)
#define PAGE_BASE_LANGDIST_HEIGHT  (34)

#define PAGE_BASE_LANGIMG_BEGIN_H (80 + 68)
#define PAGE_BASE_LANGIMG_BEGIN_V (121)

#define PAGE_AI_ANSWER_BEGIN_H  (80 + 28)
#define PAGE_AI_ANSWER_WIDTH    (400)
#define PAGE_AI_QUSTION_WIDTH   (273)
#define PAGE_AI_QUSTION_BEGIN_H (80 + 179)
#define PAGE_AI_ROBOT_BEGIN_H   (375)
#define PAGE_AI_ROBOT_WIDTH     (400)
#define PAGE_AI_ROBOT_HEIGHT    (34)

/**
 * @brief Page visible area size
 *
 * Maximum height and width of the visible area
 */
#define PAGE_BASE_VIEW_HIGHEST_H  (120 + 40)
#define PAGE_BASE_VIEW_LOWEST_H   (480 - PAGE_BASE_CONTENT_BOTTOM)
#define PAGE_BASE_VIEW_MAX_HEIGHT (PAGE_BASE_VIEW_LOWEST_H - PAGE_BASE_VIEW_HIGHEST_H)
#define PAGE_BASE_VIEW_MAX_WIDTH  (480 - 28 * 2)

/**
 * @brief Music page layout parameters
 *
 * Content area coordinates of the music page
 */
#define PAGE_MUSIC_BEGIN_H       (PAGE_BASE_VIEW_HIGHEST_H + LVGL_UI_MARGIN_60)
#define PAGE_MUSIC_LYRIC_BEGIN_H (PAGE_MUSIC_BEGIN_H + LVGL_UI_MARGIN_60)

/**
 * @brief Top notification area size
 *
 * Coordinates and size of the top notification area
 */
#define PAGE_BASE_NOTICE_BEGIN_V (10)
#define PAGE_BASE_NOTICE_MAX_WIDTH   (460)
#define PAGE_BASE_NOTICE_MAX_HEIGHT  (110)

/**
 * @brief SST page parameters
 *
 * Width definitions for left, right and center areas
 */
#define PAGE_SST_LEFT_BEGIN_H   (80 + 28)
#define PAGE_SST_LEFT_WIDTH     (400)
#define PAGE_SST_RIGHT_BEGIN_H  (80 + 179)
#define PAGE_SST_RIGHT_WIDTH    (360)
#define PAGE_SST_STATUS_WIDTH   (180)

/**
 * @brief Reader page base size
 *
 * Base size and coordinates of the reader page
 */
#define PAGE_READER_BASE_WIDTH  (424)
#define PAGE_READER_BASE_HEIGHT (300)
#define PAGE_READER_BASE_X      (80 + 28)
#define PAGE_READER_BASE_Y      (130)
