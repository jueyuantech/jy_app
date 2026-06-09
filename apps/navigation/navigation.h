/**
 * @file navigation.h
 * @brief Navigation 应用公共接口声明。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define IMG_DIR_SIZE (48)

#define LOADING_FRAME_WIDTH 240
#define LOADING_FRAME_HEIGHT 140

#define SPEED_FRAME_WIDTH 80
#define SPEED_FRAME_HEIGHT 80

#define HEALTH_FRAME_WIDTH 110
#define HEALTH_FRAME_HEIGHT 65

#define INFO_FRAME_WIDTH 440
#define INFO_FRAME_HEIGHT 80

#define DRIVE_TYPE_CAR 1
#define DRIVE_TYPE_WALK 2
#define DRIVE_TYPE_BICY	3

#define SPEED_FRAME_BASEHEIGHT (LV_VER_RES - PAGE_BASE_CONTENT_BOTTOM - INFO_FRAME_HEIGHT - SPEED_FRAME_HEIGHT)



/** @defgroup app_navigation Navigation App @{ */

typedef struct app_page_t app_page_t;

#include "common/app_framework/app_manager.h"
#include "message.h"
#include "system/system.h"
#include "floatair_fs.h"
#include <lvgl/lvgl.h>

/**
 * @brief 注册 Navigation 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool navigation_app_register(void);

/**
 * @brief Route navigation command
 * @param node Message node
 * @param msg Message pack instance
 * @return true Message parsed successfully
 * @return false Message parsed failed
 */
bool navigation_route_cmd(mpack_node_t node, msg_pack_t* msg);

void navigation_map_clear(void);
void navigation_map_update_info(int navMode,
                                const char* nextRoadName,
                                const char* curStepRetainDistance,
                                const char* remainDistance,
                                const char* remainTime,
                                const char* speed);
void navigation_map_update_bpm(const char* bmp);
void navigation_map_update_spo(const char* spo);
void navigation_map_update_dir_icon_bin(const uint8_t* data, size_t size);

/**
 * @brief 获取 Navigation 页面描述符。
 * @return 返回 Navigation 页面描述符。
 */
const app_page_t* navigation_page_get(void);
/** @} */
#ifdef __cplusplus
}
#endif
