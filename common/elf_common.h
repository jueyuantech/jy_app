#pragma once
#include <nuttx/compiler.h>
/*
 * elf_common.h
 *
 * 公共数据结构与常量定义：
 * - ELF app 与系统/各核之间的消息队列协议（消息头、payload 组织方式等）
 * - 工厂区/系统管理相关的共享结构体（例如 bt_info、jyt_section_data_t 等）
 *
 * 特别注意    这个文件由平台发布，如果需要修改请自行做好版本管理
 * 特别注意    这个文件由平台发布，如果需要修改请自行做好版本管理
 * 特别注意    这个文件由平台发布，如果需要修改请自行做好版本管理
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifndef FAR
#define FAR
#endif

/*
 * @brief: system event type
*/
enum SYSTEM_EVENT_TYPE {
    SET_IMU_SINGLE_TAP,
    SET_IMU_DOUBLE_TAP,
    SET_IMU_TILT,
    SET_IMU_WOM,
    SET_IMU_R2W,
    SET_IMU_SMD,
    SET_IMU_DATA_UPDATE,

    SET_BAT_SOC_CHANGED, // SOC and chargering mode.
    SET_BAT_VOLT_CHANGED,
    SET_CHARGER_STATE_CHANGED,

    SET_SLIDE_FORWARD,
    SET_SLIDE_BACKWORD,
    SET_FORCE_SINGLE_CLICK,
    SET_FORCE_DOUBLE_CLICK,
    SET_FORCE_TRI_CLICK,
    SET_FORCE_LONG_PRESSED,
    SET_IED_WEAR_ON,
    SET_IED_REMOVED,
    SET_KWS_HIT,
    
    SET_REPORT_DEVICE_STATE,
    SET_JYT_SIBLING_SYNC = 200,
    SET_BT_CALL_SETUP_EVENT,
    SET_BT_AVRCP_POSITION_CHANGED,
    SET_TWS_LINK_BROKEN = 1000,
    SET_JYP_HOST_DISCONNECTED,
    SET_JYP_HOST_CONNECTED,
    SET_JYT_LOW_BATTERY_WARNING, // battery
    SET_JYT_TIMER_TRIGGER,
    SET_JYT_BT_VISIBLE_CHANGED,
    SET_ANCS_EVENT,                // ANCS通知事件(Added/Modified/Removed + 详情)
    SET_JYT_REFRESH_UI_REQ,

};
enum TILT_DIRECTION {
    TILT_DIRECTION_NONE,
    TILT_DIRECTION_UP,
    TILT_DIRECTION_DOWN,
    TILT_DIRECTION_LEFT,
    TILT_DIRECTION_RIGHT,
};

typedef enum {
    DEV_CALL_CTRL=0,
    DEV_MLED_CTRL,
    DEV_KWS_CTRL,
    DEV_ALS_CTRL,
    DEV_BT_VISIBLE_CTRL,
    DEV_WEARING_CTRL,
    DEV_MARHONY_CTRL,
    DEV_SOUNDPLUS_CTRL,
} dev_type_t;

typedef struct {
        uint8_t  dev_type;  
        uint8_t  control_code;
        uint16_t data;
} dev_ctl_cmd_t;

/*
DEV_SOUNDPLUS_CTRL:
ctrl_code:   sve_mode
     data:   fwd_channel
*/
 
/*
 * @brief: battery state
*/
union bat_state_t {
    uint32_t value;
    struct {
        uint8_t soc;          // 0-100%   only useful after calibrated.
        uint8_t charger_mode; // 0: not charging,  2 bit;
        uint16_t voltage_mv;  // in millivolt
    } bat_chg_combo;
};

/*
 * @brief: elf message type
*/
enum elf_msg_type {
    EMT_HOST_MPACK_MSG = 0,
    EMT_SYSTEM_EVENT,
    EMT_SYSTEM_EVENT_WITH_PAYLOAD,
    EMT_SYSTEM_EMERG_MSG,
    // xxx
    // xxx
};

/*
 * @brief: elf message header
*/
typedef struct {
    uint8_t msg_type;    // 消息类型： 0: mpack. 1: system event
    uint8_t simple_data;    // for alignment.
    uint16_t event_type; // TBF: 没有对齐
} JYT_ELF_Msg_Header;

/*
 * @brief: elf message queue message
 *
 * @note:  EMT_HOST_MPACK_MSG:  host mpack message
 *               payload:  mpack message,length is payload_len.
 *               payload_len: payload length.   
 * 
 *         EMT_SYSTEM_EVENT:  system event message, ignore payload. and payload_len is 0.
 *               event_type:  system event type defined in enum SYSTEM_EVENT_TYPE.
 * 
 *         EMT_SYSTEM_EMERG_MSG:   system notify message. 
 *               payload:     contain the text message to be displayed.
 *               payload_len: payload length.
 * 
*/
typedef aligned_data(4) struct {
    JYT_ELF_Msg_Header Header;
    uint16_t payload_len;
    uint8_t payload[]; // ptr u32
} JYT_ELF_MQ_MSG, *pELF_Msg;

/*
 * @brief: bluetooth info
*/
typedef struct bt_info_struct {
    char bt_name[64];
    char bt_addr[20];
    char ble_addr[20];
    char ble_name[32];
} bt_info;

/*
 * @brief: jyt section data
*/
typedef struct {
    int8_t fixed_x;
    int8_t fixed_y;
} IVC12_PRESET_PARA_T;

/*
 * @brief: jyt section data
    upgrade 
*/
typedef struct {
    /* data */
    uint8_t jyt_psn[64];               // 64
    uint8_t jyt_ssn[64];              // 64
    uint8_t jyt_manufacture[32];      // 32
    uint8_t jyt_model[32];            // 32
    uint8_t jyt_edition[128];         // 128
    uint8_t jyt_default_volumn;       // 1
    uint16_t jyt_default_brightness;  // 2
} jyt_section_data_t;                

typedef aligned_data(4) struct {
    time_t  time_now;
    uint8_t host_connected;
    uint8_t speaker_connected;
    uint8_t wearing_detection;
    uint8_t marhony_detection;
    uint8_t reserved[4];        // wearing detection/kws/marhony detection/
}jyt_device_state_t ;
/*
 * @brief: ioctl function
*/
extern int ioctl(int file_desc, int req, ...);

/**
 * @brief create and start a period self-trigger timer. 
 * @param timeout_ms:  timer interval in ms.
 * @param timer_id:  timer id.
 * @param auto_destroy:  0: manual destroy. 1: auto destroy.
 * @return the handler of the timer.
 */
extern void * jyt_timer_create_and_start(uint32_t timeout_ms,uint32_t timer_id,int auto_destroy);

/**
 * @brief stop a timer.
 * @param timer: timer handler.
 */
extern void jyt_timer_stop(void* timer);

/**
 * @brief restart a timer.
 * @param timer: timer handler.
 */
extern void jyt_timer_restart(void* timer);

/**
 * @brief delete a timer.
 * @param timer: timer handler.
 */
extern void jyt_timer_delete(void* timer);

/**
 * @brief get bluetooth info
 * @param btinfo: pointer to bt_info struct.
 */
extern void jyt_get_bt_info(bt_info* btinfo);

/**
 * @brief get factory test info
 * @param ftinfo: pointer to jyt_section_data_t struct.
 */
extern void jyt_get_ft_info(FAR jyt_section_data_t* ftinfo);

/**
 * @brief set string format date time to system 
 * @param time_str:  date time string.
 * @param format:    date time format . 
 * @return  0: succeed.  other: failed.
 * @example  set_system_time_from_string("2026-01-20 18:55:00", "%Y-%m-%d %H:%M:%S");
 */
extern int set_system_time_from_string(const char* time_str, const char* format);

/**
 * @brief send message to host app via established bluetooth link 
 * @param buf:  buffer to be send (encoded with mpack)
 * @param bufsize: buffer size in bytes. 
 */
extern void send2host(void *buf, uint32_t bufsize);

/**
 * @brief caculate crc16_ccitt
 * @param data:  buffer used to calculate crc16
 * @param len:   data length in bytes.
 * @return crc16
 */
extern uint16_t hal_crc16_ccitt(void *data, uint16_t len);

extern uint16_t hal_crc16_ccitt_v1(void *data, uint16_t len,uint16_t init);

/**
 * @brief caculate crc32
 * @param data: buffer used to calculate crc32
 * @param len:  data length in bytes
 * @return crc32 value
 */
extern uint32_t hal_crc32(void *data, uint16_t len);

/**
 * @brief get system tick count in Micro seconds
 * @return  tick count in us
 */
extern long int GetTimeUs(void);

/**
 * @brief  play system pre-defined prompter tone
 *           
 * @param  play_id, the tone index please refer to 
 *            
 */
void play_prompt(uint8_t play_id);

 
#ifdef __cplusplus
}
#endif
