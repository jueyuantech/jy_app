/**
 * @file system_file_transfer.h
 * @brief 系统文件写入进度通知接口。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 文件写入进度回调。
 *
 * @param path 正在写入的文件完整路径。
 * @param written 已写入字节数。
 * @param total 文件总字节数。
 * @param user_data 注册回调时传入的用户数据。
 * @return 无返回值。
 */
typedef void (*system_file_transfer_progress_cb_t)(const char* path,
                                                   uint32_t written,
                                                   uint32_t total,
                                                   void* user_data);

/**
 * @brief 注册系统文件写入进度回调。
 *
 * 当前仅保留一个业务回调，后注册者会覆盖旧回调。
 *
 * @param cb 进度回调；传 `NULL` 表示清空当前回调。
 * @param user_data 用户数据，会在回调时原样传回。
 * @return 无返回值。
 */
void system_file_transfer_set_progress_callback(system_file_transfer_progress_cb_t cb,
                                                void* user_data);

/**
 * @brief 清空匹配的系统文件写入进度回调。
 *
 * 只有当前回调与 `cb`、`user_data` 同时匹配时才清空，避免误清其他页面
 * 后续注册的回调。
 *
 * @param cb 待清空的进度回调。
 * @param user_data 注册时传入的用户数据。
 * @return 无返回值。
 */
void system_file_transfer_clear_progress_callback(system_file_transfer_progress_cb_t cb,
                                                  void* user_data);

/**
 * @brief 查询当前是否处于系统文件写入传输中。
 *
 * 文件首包写入成功后进入传输中，完整文件校验完成后退出传输中。
 *
 * @return `true` 表示正在传输文件，`false` 表示当前没有文件传输。
 */
bool system_file_transfer_is_in_progress(void);

/**
 * @brief 查询上传进度是否允许被业务页面显示。
 * @return `true` 表示允许显示上传进度，`false` 表示需要隐藏上传进度。
 */
bool system_file_transfer_is_upload_progress_visible(void);

/**
 * @brief 设置上传进度显隐许可，并用最近一次传输进度刷新当前页面。
 *
 * 该接口只在文件传输中生效，调用方应先确认当前页面支持显示上传进度。
 *
 * @param visible `true` 表示允许显示上传进度，`false` 表示隐藏上传进度。
 * @return `true` 表示设置成功，`false` 表示当前不在文件传输中。
 */
bool system_file_transfer_set_upload_progress_visible(bool visible);

#ifdef __cplusplus
}
#endif
