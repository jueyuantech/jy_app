/**
 * @file system_runtime_state.h
 * @brief 系统运行时状态同步接口声明
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#pragma once

#include "system/system_runtime_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief KWS 命中事件的软件拦截原因。
 */
typedef enum {
    SYSTEM_KWS_INTERCEPT_REASON_APP_POLICY = 1u << 0,    ///< 当前 App 策略禁止响应 KWS。
    SYSTEM_KWS_INTERCEPT_REASON_SPEECH_ACTIVE = 1u << 1, ///< Speech 正在运行。
    SYSTEM_KWS_INTERCEPT_REASON_PHONE_ACTIVE = 1u << 2,  ///< Phone 通话流程正在进行。
    SYSTEM_KWS_INTERCEPT_REASON_PROMPTER_RUNNING = 1u << 3, ///< 题词正在运行。
} system_kws_intercept_reason_t;

/**
 * @brief 设置或清除一项 KWS 软件拦截原因。
 * @param[in] reason 拦截原因。
 * @param[in] blocked `true` 表示设置，`false` 表示清除。
 * @return 无返回值。
 */
void system_runtime_state_set_kws_intercept(system_kws_intercept_reason_t reason,
                                            bool blocked);

/**
 * @brief 缓存 ANCS 提供的第三方来电联系人和来源，供随后的 HFP 状态使用。
 * @param[in] caller_name 第三方来电联系人名称。
 * @param[in] source_id iOS AppIdentifier。
 * @return 无返回值。
 */
void system_runtime_state_set_ancs_call_info(const char* caller_name,
                                             const char* source_id);

/**
 * @brief 处理设备状态消息并同步时间，启动首次额外同步蓝牙连接态。
 * @param[in] msg 设备状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_device_state(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 处理 ALS 原始光感数据并在自动亮度开启时刷新屏幕亮度。
 * @param[in] msg ALS 原始数据消息，payload 为 uint32_t 原始光感值。
 * @return `true` 表示处理成功，`false` 表示消息格式错误。
 */
bool system_update_als_raw_data(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 将最近一次缓存的 ALS 自动亮度立即应用到当前亮屏。
 * @return `true` 表示无需更新或更新成功，`false` 表示亮度上报失败。
 */
bool system_runtime_state_apply_auto_brightness(void);
/**
 * @brief 获取 LCD 亮屏恢复时应使用的亮度。
 * @return 自动亮度开启且已有 ALS 档位时返回自动亮度，否则返回保存的手动亮度。
 */
uint8_t system_runtime_state_get_lcd_resume_brightness(void);
/**
 * @brief 处理 KWS 命中事件，通过软件策略过滤后唤醒屏幕和上报命中。
 * @param[in] msg KWS 事件消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_kws_state(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 处理来电状态消息并控制来电通知显隐。
 * @param[in] msg 来电状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_handle_call_setup_event(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 缓存主从分侧电池快照，两侧均已上报后选择较低电量侧同步状态栏及上报。
 * @param[in] msg payload 为 SDK 定义的 jyt_battery_report_t。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_bat_status(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 更新指定侧的 case 充电覆盖状态，进入时补报盒子电量，退出时清零充电并等待该侧新上报。
 * @param[in] side 主侧或从侧。
 * @param[in] in_case 该侧配件是否为 case。
 * @return 无返回值。
 */
void system_update_battery_case_state(jyt_battery_side_t side, bool in_case);
/**
 * @brief 读取指定侧的电池快照，充电模式已按该侧 case 状态修正。
 * @param[in] side 主侧或从侧。
 * @param[out] state 返回该侧电量、充电模式及电压，失败时不修改。
 * @return `true` 表示已取得快照，`false` 表示参数无效或该侧尚未上报。
 */
bool system_get_battery_state_by_side(jyt_battery_side_t side, union bat_state_t* state);
/**
 * @brief 缓存眼镜盒电量，仅在手机已连接且当前有眼镜盒附件时上报。
 * @param[in] battery 眼镜盒电量百分比。
 * @return `true` 表示已缓存且无需发送或发送成功，`false` 表示发送失败。
 */
bool system_update_case_battery(uint8_t battery);
/**
 * @brief 获取当前缓存充电状态。
 * @return 返回当前充电状态值。
 */
uint8_t system_get_charge_state(void);
/**
 * @brief 判断主从两侧是否都已收到有效电池快照，统一显示和协议查询的就绪条件。
 * @return 双侧均已收到有效快照时返回 true，否则返回 false。
 */
bool system_is_battery_ready(void);
/**
 * @brief 获取手机协议的聚合和双侧充电状态，未就绪时不输出默认值。
 * @param[out] state 最低电量侧是否充电，取值 0/1；失败时不修改。
 * @param[out] state_by_side bit0 表示主侧充电，bit1 表示从侧充电；失败时不修改。
 * @return 双侧数据就绪且参数有效返回 true，否则返回 false。
 */
bool system_get_charge_states(uint8_t* state, uint8_t* state_by_side);
/**
 * @brief 获取当前缓存电量。
 * @return 返回当前电量百分比。
 */
uint8_t system_get_battery(void);
/**
 * @brief 获取当前蓝牙连接状态。
 * @return `true` 表示蓝牙已连接，`false` 表示蓝牙未连接。
 */
bool system_get_btconn_state(void);
/**
 * @brief 获取蓝牙连接状态变化事件 ID。
 * @return 返回 LVGL 自定义事件 ID，事件参数为指向 `uint8_t` 连接状态的指针。
 */
uint32_t system_runtime_state_get_btconn_event(void);
/**
 * @brief 设置显式主机连接事件状态并刷新蓝牙连接态。
 * @param[in] connected `true` 表示显式事件为已连接，`false` 表示显式事件为未连接。
 * @return 无返回值。
 */
void system_set_btconn_state(bool connected);
/**
 * @brief 亮屏后清理已结束的电话弹窗，并补做灭屏期间延迟的蓝牙状态 UI 同步。
 * @return 无返回值。
 */
void system_runtime_state_flush_pending_after_screen_on(void);

#ifdef __cplusplus
}
#endif
