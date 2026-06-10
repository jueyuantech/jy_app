/**
 * @file reader.h
 * @brief Reader 应用对外接口
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "message.h"
#include "common/app_framework/app_manager.h"
#include "system/system.h"
#include <lvgl/lvgl.h>

typedef struct app_page_t app_page_t;

#define READER_DEBUG_DUMP 1

extern app_font_info_t reader_font_info;
extern const lv_font_t* reader_font;

/** @defgroup app_reader Reader App @{ */

/**
 * @brief 注册 Reader 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool reader_app_register(void);

/**
 * @brief 重置 Reader 视图状态。
 * @return 无返回值。
 */
void reader_view_reset(void);
const app_page_t* reader_page_get(void);

/**
 * @brief 路由 Reader 命令
 * @param node mpack 节点
 * @param msg 消息包
 * @return `true` 表示处理成功
 */
bool reader_route_cmd(mpack_node_t node, msg_pack_t* msg);

bool reader_config_ensure(void);
bool reader_config_reset_to_default(void);

/**
 * @brief 清空 Reader 当前文本
 */
void reader_text_clear(void);
/**
 * @brief 设置 Reader 文本文件路径
 * @param path 文件路径
 * @return `true` 表示加载成功
 */
bool reader_text_set_file(const char* path);
/**
 * @brief 设置当前页索引（基于滚动位置）
 * @param page_idx 页索引
 * @return `true` 表示跳转成功
 */
bool reader_text_set_page(uint32_t page_idx);
/**
 * @brief 获取当前页码（从 1 开始显示）
 * @return 当前页码
 */
uint32_t reader_text_current_page(void);
/**
 * @brief Reader 文本按页向上滚动
 * @return `true` 表示发生滚动
 */
bool reader_text_page_up(void);
/**
 * @brief Reader 文本按页向下滚动
 * @return `true` 表示发生滚动
 */
bool reader_text_page_down(void);
/**
 * @brief 显示提示文本
 * @param msg 提示消息
 */
void reader_text_show_msg(const char* msg);
/**
 * @brief 重置 Reader 文本分页状态
 */
void reader_text_page_init(void);
/**
 * @brief 设置 Reader 文本样式配置
 * @param font_info 字体配置
 */
void reader_text_set_font(app_font_info_t *font_info);
 
/**
 * @brief 从配置文件读取 Reader 字体配置
 * @return `true` 表示读取并应用成功
 */
bool reader_font_init_from_config(void);
/**
 * @brief 更新 Reader 字体配置并写回配置文件
 * @param font_info 字体配置
 * @return `true` 表示更新成功
 */
bool reader_font_update_and_save(app_font_info_t *font_info);
/** @} */
#ifdef __cplusplus
}
#endif
