/**
 * @file system.h
 * @brief 系统模块公共接口声明
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once
/** @defgroup app_system System App @{ */

#include "cJSON.h"
#include "floatair_dbg.h"
#include "message.h"
#include "common/app_framework/app_router.h"
#include "system/system_runtime_input.h"
#include "system/system_runtime_state.h"
#include "system/system_runtime_types.h"
#include "system/system_runtime_ui.h"
#include "system_res.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取系统配置文件路径。
 * @return 返回系统配置文件路径。
 */
extern const char* system_config_path(void);

/**
 * @brief 初始化 system 模块。
 * @return 无返回值。
 */
void system_init(void);
/**
 * @brief 反初始化 system 模块。
 * @return 无返回值。
 */
void system_deinit(void);

/**
 * @brief 获取是否启用新手引导。
 * @return `true` 表示启用，`false` 表示未启用。
 */
bool system_config_get_userguide(void);
/**
 * @brief 设置是否启用新手引导。
 * @param[in] userguide 引导开关状态。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_userguide(bool userguide);
/**
 * @brief 获取新手引导是否已完成。
 * @return `true` 表示已完成，`false` 表示未完成。
 */
bool system_config_get_userguide_finish(void);
/**
 * @brief 设置新手引导完成状态。
 * @param[in] finish 新手引导完成状态。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_userguide_finish(bool finish);
/**
 * @brief 获取语言选择流程是否已完成。
 * @return `true` 表示已完成，`false` 表示未完成。
 */
bool system_config_get_langselection_finish(void);
/**
 * @brief 设置语言选择完成状态并写入当前语言。
 * @param[in] curlang 当前语言字符串。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_langselection_finish(const char* curlang);

/**
 * @brief 获取佩戴检测开关状态。
 * @return `true` 表示开启，`false` 表示关闭。
 */
bool system_config_get_wear_detection_enabled(void);
/**
 * @brief 设置佩戴检测开关状态。
 * @param[in] wear_detection_enabled 佩戴检测开关状态。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_wear_detection_enabled(bool wear_detection_enabled);
/**
 * @brief 获取触控板开关状态。
 * @return `true` 表示开启，`false` 表示关闭。
 */
bool system_config_get_touchpad_enabled(void);
/**
 * @brief 设置触控板开关状态。
 * @param[in] touchpad_enabled 触控板开关状态。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_touchpad_enabled(bool touchpad_enabled);
/**
 * @brief 获取通知开关状态。
 * @return `true` 表示开启，`false` 表示关闭。
 */
bool system_config_get_notification_enabled(void);
/**
 * @brief 设置通知开关状态。
 * @param[in] notification_enabled 通知开关状态。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_notification_enabled(bool notification_enabled);
/**
 * @brief 获取关键词唤醒开关状态。
 * @return `true` 表示开启，`false` 表示关闭。
 */
bool system_config_get_keyword_spotting_enabled(void);
/**
 * @brief 设置关键词唤醒开关状态。
 * @param[in] keyword_spotting_enabled 关键词唤醒开关状态。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_keyword_spotting_enabled(bool keyword_spotting_enabled);
/**
 * @brief 获取闲置检测开关状态。
 * @return `true` 表示开启，`false` 表示关闭。
 */
bool system_config_get_idle_detection_enabled(void);
/**
 * @brief 设置闲置检测开关状态。
 * @param[in] idle_detection_enabled 闲置检测开关状态。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_idle_detection_enabled(bool idle_detection_enabled);
/**
 * @brief 获取抬头/低头手势配置。
 * @param[out] config 抬头/低头手势配置输出指针。
 * @return `true` 表示读取成功，`false` 表示参数无效。
 */
bool system_config_get_head_gesture_config(system_head_gesture_config_t* config);
/**
 * @brief 设置抬头/低头手势配置。
 * @param[in] config 抬头/低头手势配置。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_head_gesture_config(const system_head_gesture_config_t* config);
/**
 * @brief 获取显示模式配置。
 * @return 返回显示模式值。
 */
uint8_t system_config_get_displayMode(void);
/**
 * @brief 设置显示模式配置。
 * @param[in] displayMode 显示模式值。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_displayMode(uint8_t displayMode);
/**
 * @brief 获取显示距离配置。
 * @return 返回显示距离值。
 */
uint32_t system_config_get_displaydistance(void);
/**
 * @brief 设置显示距离配置。
 * @param[in] distance 显示距离值。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_displaydistance(uint32_t distance);
/**
 * @brief 获取显示弹窗深度配置。
 * @return 返回弹窗深度值。
 */
uint32_t system_config_get_displaypopupdepth(void);
/**
 * @brief 设置显示弹窗深度配置。
 * @param[in] depth 弹窗深度值。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_displaypopupdepth(uint32_t depth);
/**
 * @brief 获取显示层级配置。
 * @return 返回显示层级值。
 */
uint32_t system_config_get_displaylevel(void);
/**
 * @brief 设置显示层级配置。
 * @param[in] level 显示层级值。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_displaylevel(uint32_t level);
/**
 * @brief 将显示层级配置转换为实际距离值。
 * @param[in] level 显示层级值。
 * @return 返回对应的距离常量。
 */
int system_get_displaylevel_value(uint32_t level);
/**
 * @brief 获取 LCD 息屏超时时间。
 * @return 返回超时时间，单位为秒。
 */
uint16_t system_config_get_lcd_sleep_timeout(void);
/**
 * @brief 设置 LCD 息屏超时时间。
 * @param[in] lcd_sleep_timeout 超时时间，单位为秒。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_lcd_sleep_timeout(uint16_t lcd_sleep_timeout);
/**
 * @brief 获取无操作超时时间。
 * @return 返回超时时间，单位为秒。
 */
uint16_t system_config_get_inactivity_timeout(void);
/**
 * @brief 设置无操作超时时间。
 * @param[in] inactivity_timeout 超时时间，单位为秒。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_inactivity_timeout(uint16_t inactivity_timeout);
/**
 * @brief 获取深度休眠超时时间。
 * @return 返回超时时间，单位为秒。
 */
uint16_t system_config_get_deep_sleep_timeout(void);
/**
 * @brief 设置深度休眠超时时间。
 * @param[in] deep_sleep_timeout 超时时间，单位为秒。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_deep_sleep_timeout(uint16_t deep_sleep_timeout);
/**
 * @brief 获取背光自动调节开关状态。
 * @return `true` 表示开启，`false` 表示关闭。
 */
bool system_config_get_bl_auto(void);
/**
 * @brief 设置背光自动调节开关状态。
 * @param[in] bl_auto 背光自动调节开关状态。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_bl_auto(bool bl_auto);
/**
 * @brief 获取屏幕亮度配置。
 * @return 返回亮度值，范围 0-255。
 */
uint8_t system_config_get_brightness(void);
/**
 * @brief 设置屏幕亮度配置。
 * @param[in] brightness 亮度值，范围 0-255。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_brightness(uint8_t brightness);
/**
 * @brief 获取当前语言配置。
 * @return 返回当前语言字符串。
 */
char* system_config_get_curlang(void);
/**
 * @brief 设置当前语言配置。
 * @param[in] curlang 当前语言字符串。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_curlang(char* curlang);
/**
 * @brief 获取首页应用名称。
 * @return 返回首页应用名称字符串。
 */
char* system_config_get_home_app(void);
/**
 * @brief 设置首页应用名称。
 * @param[in] home_app 首页应用名称字符串。
 * @return `true` 表示保存成功，`false` 表示保存失败。
 */
bool system_config_set_home_app(char* home_app);

/**
 * @brief 加载系统配置文件。
 * @return `true` 表示加载成功，`false` 表示加载失败。
 */
bool system_cfgfile_load(void);
/**
 * @brief 卸载系统配置文件。
 * @return `true` 表示卸载成功，`false` 表示卸载失败。
 */
bool system_cfgfile_unload(void);
/**
 * @brief 更新系统配置文件。
 * @return `true` 表示更新成功，`false` 表示更新失败。
 */
bool system_cfgfile_update(void);
/**
 * @brief 打印当前系统配置。
 * @return 无返回值。
 */
void system_cfgfile_dump(void);
bool system_cfgfile_rebuild_default(void);
bool system_cfgfile_reset_to_default(void);

typedef bool (*system_factoryreset_handler_t)(void);
bool system_factoryreset_register(const char* name, system_factoryreset_handler_t handler);
void system_factoryreset_invoke(void);

/**
 * @brief 分发 system 模块消息命令。
 * @param[in] node mpack 节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_route_cmd(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief 处理心跳事件。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_heart_beat(void);
/**
 * @brief 处理保活事件。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_keep_alive(void);
/**
 * @brief 判断当前 popup 状态下是否允许处理指定 host 消息。
 * @param[in] msg 已解析的 host 消息头。
 * @return `true` 表示允许继续处理，`false` 表示应直接返回 `ErrNotReady`。
 */
bool system_host_message_allowed_when_popup_active(const msg_pack_t* msg);
/**
 * @brief 上报触摸事件。
 * @param[in] code LVGL 事件码。
 * @return `true` 表示上报成功，`false` 表示上报失败。
 */
bool system_report_touch_event(lv_event_code_t code);
/**
 * @brief 上报当前视图变化。
 * @param[in] view_name 当前视图名称。
 * @return `true` 表示上报成功，`false` 表示上报失败。
 */
bool system_report_view_change(const char* view_name);
/**
 * @brief 上报 kws 命中。
 * @return `true` 表示上报成功，`false` 表示上报失败。
 */
bool system_report_kws_hit(void);
/**
 * @brief 结束关键词唤醒上报回包等待。
 * @return 无返回值。
 */
void system_report_kws_hit_response_finish(void);
/**
 * @brief 上报 assistant 已关闭。
 * @return `true` 表示上报成功，`false` 表示上报失败。
 */
bool system_report_assistant_close(void);
/**
 * @brief 上报系统亮灭屏状态。
 * @param[in] state 系统状态值。
 * @return `true` 表示上报成功，`false` 表示上报失败。
 */
bool system_report_sys_state(uint8_t state);
/**
 * @brief 上报充电状态。
 * @param[in] state 充电状态值。
 * @return `true` 表示上报成功，`false` 表示上报失败。
 */
bool system_report_charge_state(uint8_t state);
/**
 * @brief 上报电量信息。
 * @param[in] battery 电量百分比。
 * @return `true` 表示上报成功，`false` 表示上报失败。
 */
bool system_report_battery(uint32_t battery);

/**
 * @brief 获取下一条上报消息序号。
 * @return 返回下一条消息序号。
 */
uint32_t system_report_next_sequence(void);
/**
 * @brief 请求底层返回最新设备状态。
 * @return `true` 表示请求已发送，`false` 表示发送失败。
 */
bool system_request_device_state(void);
/**
 * @brief 请求底层执行设备控制命令。
 * @param[in] cmd 设备控制命令。
 * @return `true` 表示请求已发送，`false` 表示发送失败。
 */
bool system_request_device_control(const dev_ctl_cmd_t* cmd);
/**
 * @brief 请求 OS 层切换系统休眠许可。
 * @param[in] enable `true` 表示允许休眠，`false` 表示禁止休眠。
 * @return `true` 表示请求已发送，`false` 表示发送失败。
 */
bool system_request_os_sleep(bool enable);
/**
 * @brief 请求底层切换 KWS 关键词唤醒状态。
 * @param[in] enabled `true` 表示启用 KWS，`false` 表示禁用 KWS。
 * @return `true` 表示请求已发送，`false` 表示发送失败。
 */
bool system_request_keyword_spotting_enabled(bool enabled);
/**
 * @brief 请求底层更新抬头/低头 IMU 触发阈值。
 * @param[in] heads_up_threshold 抬头触发 pitch 阈值。
 * @param[in] heads_down_threshold 低头触发 pitch 阈值。
 * @return `true` 表示请求已发送，`false` 表示发送失败。
 */
bool system_request_imu_threshold(float heads_up_threshold, float heads_down_threshold);
/**
 * @brief 将 app 配置中需要底层感知的部分同步到底层。
 * @return 无返回值。
 */
void system_sync_config_to_device(void);
/**
 * @brief 请求底层切换蓝牙可见性。
 * @param[in] visibility 蓝牙可见性，取值见 `SMMAN_BT_VISIBILITY`。
 * @return `true` 表示请求已发送，`false` 表示发送失败。
 */
bool system_request_bt_visibility(uint8_t visibility);

bool system_request_bt_reset_pair(void);
/**
 * @brief 周期向底层请求最新设备时间。
 * @return 无返回值。
 */
void system_update_time(void);
/**
 * @brief 判断文件名是否为支持的图片格式。
 * @param[in] name 文件名。
 * @return `true` 表示是支持的图片格式，`false` 表示不是。
 */
bool system_is_image_file(const char *name);

/**
 * @brief 初始化息屏定时器。
 * @return 无返回值。
 */
void app_sleep_timer_init(void);
/**
 * @brief 重置息屏定时器。
 * @return 无返回值。
 */
void app_sleep_timer_reset(void);

/**
 * @brief 打印蓝牙信息缓存。
 * @return 无返回值。
 */
void system_dump_bt_info(void);
/**
 * @brief 打印工厂区关键设备信息。
 * @return 无返回值。
 */
void system_dump_jyt_section(void);

/**
 * @brief 获取当前系统亮灭屏状态。
 * @return `1` 表示亮屏，`0` 表示灭屏。
 */
uint8_t system_get_sys_state(void);
/**
 * @brief 设置当前系统亮灭屏状态。
 * @param[in] state 目标系统状态值。
 * @return 无返回值。
 */
void system_set_sys_state(uint8_t state);
/**
 * @brief 获取设备总存储容量。
 * @return 返回总容量，单位为字节。
 */
uint32_t system_get_rom_total(void);
/**
 * @brief 获取设备已用存储容量。
 * @return 返回已用容量，单位为字节。
 */
uint32_t system_get_rom_used(void);
/**
 * @brief 获取设备剩余存储容量。
 * @return 返回剩余容量，单位为字节。
 */
uint32_t system_get_rom_remaining(void);
/**
 * @brief 获取制造商字符串。
 * @return 返回制造商字符串。
 */
const char* system_get_manufacture(void);
/**
 * @brief 获取产品型号字符串。
 * @return 返回产品型号字符串。
 */
const char* system_get_model(void);
/**
 * @brief 获取版本说明字符串。
 * @return 返回版本说明字符串。
 */
const char* system_get_edition(void);
/**
 * @brief 获取产品序列号字符串。
 * @return 返回产品序列号字符串。
 */
const char* system_get_sn(void);
/**
 * @brief 获取短序列号字符串。
 * @return 返回短序列号字符串。
 */
const char* system_get_ssn(void);
/**
 * @brief 获取蓝牙地址字符串。
 * @return 返回蓝牙地址字符串。
 */
const char* system_get_btaddr(void);
/**
 * @brief 获取蓝牙名称字符串。
 * @return 返回蓝牙名称字符串。
 */
const char* system_get_btname(void);
/**
 * @brief 获取 BLE 地址字符串。
 * @return 返回 BLE 地址字符串。
 */
const char* system_get_bleaddr(void);
/**
 * @brief 获取 BLE 名称字符串。
 * @return 返回 BLE 名称字符串。
 */
const char* system_get_blename(void);
/**
 * @brief 获取应用固件版本号。
 * @return 返回应用固件版本字符串。
 */
const char* system_get_fwver(void);
/**
 * @brief 获取底层系统版本号。
 * @return 返回底层系统版本字符串。
 */
const char* system_get_bthver(void);
/**
 * @brief 获取协议版本号。
 * @return 返回协议版本字符串。
 */
const char* system_get_protocolver(void);
/**
 * @brief 获取默认音量配置。
 * @return 返回默认音量值。
 */
uint8_t system_get_default_volumn(void);
/**
 * @brief 获取默认亮度配置。
 * @return 返回默认亮度值。
 */
uint16_t system_get_default_brightness(void);
/**
 * @brief 设置默认亮度配置。
 * @param[in] brightness 目标默认亮度。
 * @return 无返回值。
 */
void system_set_default_brightness(uint16_t brightness);

/** @} */
#ifdef __cplusplus
}
#endif
