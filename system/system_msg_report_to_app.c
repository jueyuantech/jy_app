/**
 * @file system_msg_ind.c
 * @brief System indicator message
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include <time.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "common/app_framework/app_router.h"
#include "common/widgets/toast.h"
#include "app_def.h"
#include "app_lcd.h"
#include "system/system.h"
#include "system/system_res.h"
#include "system/system_timer.h"

#include <inttypes.h>
#include <string.h>
#include "sys_adapter.h"
#include "system/system.h"

/*
cmd : onTouchEvent
map(1) {
  "event": uint8(1)
}
value:  说明：1-单击, 2-双击, 3-长按, 4-上滑, 5-下滑, 6-左滑, 7-右滑
*/

/*
cmd : onViewChangedByName
map(1) {
  "viewName": str("Home")
}
*/

/*
key:"battery"
value:  percent
*/

/*
key:"chargeState"
value:  说明：0-plugout , 1-plugin, 2- full
*/

/*
key:"sysState"
value:  说明：0-SCREEN_OFF(休眠状态), 1-SCREEN_ON(活动状态)
*/

/*
key:"brightness"
value:  0-100
*/

/*
report_pageinfo
*/

static uint32_t s_report_sequence = 0;
static uint32_t s_kws_response_timer_id = 0;

#define SYSTEM_KWS_RESPONSE_TIMEOUT_MS 3000U ///< 等待手机端处理关键词唤醒上报的超时时间。

uint32_t system_report_next_sequence(void) {
    return s_report_sequence++;
}

/**
 * @brief 处理关键词唤醒上报未收到手机端回包的超时提示。
 * @param[in] user_data 未使用。
 * @return 无返回值。
 */
static void system_report_kws_response_timeout_cb(void* user_data) {

    (void) user_data;

    s_kws_response_timer_id = 0;
    toast_show(app_get_str("TOAST_KWS_PHONE_NO_RESPONSE"));
}

/**
 * @brief 启动关键词唤醒上报回包等待定时器。
 * @return 无返回值。
 */
static void system_report_kws_response_wait_start(void) {
    uint32_t timer_id = 0;

    if (s_kws_response_timer_id != 0) {
        return;
    }

    if (system_timer_autodestroy_start(SYSTEM_KWS_RESPONSE_TIMEOUT_MS,
                                       system_report_kws_response_timeout_cb,
                                       NULL,
                                       &timer_id)) {
        s_kws_response_timer_id = timer_id;
    } else {
        floatair_warn("start kws response timer failed");
    }
}

void system_report_kws_hit_response_finish(void) {
    if (s_kws_response_timer_id == 0) {
        return;
    }

    system_timer_autodestroy_cancel(s_kws_response_timer_id);
    s_kws_response_timer_id = 0;
}

bool system_report_touch_event(lv_event_code_t code) {
    uint8_t event = 0;
    switch (code) {
        case LV_EVENT_CLICKED:
            event = SYSTEM_TOUCH_EVENT_CLICKED;
            break;
        case LV_EVENT_DCLICKED:
            event = SYSTEM_TOUCH_EVENT_DCLICKED;
            break;
        case LV_EVENT_LONG_PRESSED:
            event = SYSTEM_TOUCH_EVENT_LONG_PRESSED;
            break;
        case LV_EVENT_GESTURE_LEFT:
            event = SYSTEM_TOUCH_EVENT_GESTURE_LEFT;
            break;
        case LV_EVENT_GESTURE_RIGHT:
            event = SYSTEM_TOUCH_EVENT_GESTURE_RIGHT;
            break;
        default:
            break;
    }
    if (event == 0) {
        floatair_err("invalid touch event %d from code %d", event, code);
        return false;
    }
    floatair_info("report touch event %d from code %d", event, code);
    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onTouchEvent", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';
    
    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "event");
    mpack_write_u8(&writer->writer, event);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

bool system_report_view_change(const char* view_name) {
    const char* protocol_view_name = NULL;

    if (!view_name) {
        floatair_err("view_name is NULL");
        return false;
    }

    protocol_view_name = app_router_app_to_protocol_name(view_name);
    if (protocol_view_name == NULL) {
        return false;
    }
    floatair_info("report view change %s, app=%s", protocol_view_name, view_name);
    
    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onViewChangedByName", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';
    
    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "viewName");
    mpack_write_cstr(&writer->writer, protocol_view_name);
    mpack_finish_map(&writer->writer);
    
    return app_mpack_send_writer(writer);
}

bool system_report_kws_hit(void) {
    floatair_info("report kws hit");

    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onKeywordSpotting", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 0);
    mpack_finish_map(&writer->writer);

    bool ret = app_mpack_send_writer(writer);
    if (ret) {
        system_report_kws_response_wait_start();
    }
    return ret;
}

bool system_report_assistant_open(void) {
    floatair_info("report assistant open");

    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onAssistantOpen", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 0);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

bool system_report_assistant_close(void) {
    floatair_info("report assistant close");

    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onAssistantClose", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 0);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

bool system_report_guide_open(void) {
    floatair_info("report guide open");

    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onGuideOpen", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 0);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

bool system_report_guide_close(void) {
    floatair_info("report guide close");

    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onGuideClose", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 0);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

bool system_report_sys_state(uint8_t state, const char* trigger) {
    if (!floatair_lcd_state_is_valid((lcd_state_t)state)) {
        floatair_err("reject invalid screen state report: %u, expected 0(OFF) or 1(ON)",
                     (unsigned)state);
        return false;
    }
    if (trigger == NULL || trigger[0] == '\0') {
        floatair_err("report sys state trigger is empty");
        return false;
    }

    floatair_info("report screen state %u(%s), trigger %s",
                  (unsigned)state,
                  floatair_lcd_state_name((lcd_state_t)state),
                  trigger);
    
    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onSysStateChanged", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';
    
    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 2);
    mpack_write_cstr(&writer->writer, "sysState");
    mpack_write_u8(&writer->writer, state);
    mpack_write_cstr(&writer->writer, "trigger");
    mpack_write_cstr(&writer->writer, trigger);
    mpack_finish_map(&writer->writer);
    
    return app_mpack_send_writer(writer);
}

/**
 * @brief 向手机同时发送聚合充电状态 chargeState 和双侧位图 chargeStateBySide。
 * @param[in] state 最低电量侧的充电状态，合法范围 0-1。
 * @param[in] state_by_side bit0 为主侧充电，bit1 为从侧充电，合法范围 0-3。
 * @return true 表示发送成功，false 表示参数非法或发送失败。
 */
bool system_report_charge_state(uint8_t state, uint8_t state_by_side) {
    if (state > 1u || state_by_side > 3u) {
        floatair_err("invalid charge states: aggregate=%u by_side=%u",
                     (unsigned)state, (unsigned)state_by_side);
        return false;
    }
    floatair_info("report charge states: aggregate=%u by_side=%u",
                  (unsigned)state, (unsigned)state_by_side);
    
    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onChargeStateChanged", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';
    
    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 2);
    mpack_write_cstr(&writer->writer, "chargeState");
    mpack_write_u8(&writer->writer, state);
    mpack_write_cstr(&writer->writer, "chargeStateBySide");
    mpack_write_u8(&writer->writer, state_by_side);
    mpack_finish_map(&writer->writer);
    
    return app_mpack_send_writer(writer);
}

bool system_report_battery(uint32_t battery) {
    floatair_info("report battery %" PRIu32, battery);

    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onBatteryChanged", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "battery");
    mpack_write_u32(&writer->writer, battery);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

bool system_report_case_battery(uint8_t battery) {
    floatair_info("report case battery %u", (unsigned)battery);

    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onCaseBatteryChanged", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "battery");
    mpack_write_u8(&writer->writer, battery);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

bool system_report_brightness(uint8_t brightness) {
    floatair_info("report brightness %u", (unsigned)brightness);

    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onBrightnessChanged", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "brightness");
    mpack_write_u8(&writer->writer, brightness);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

/**
 * @brief 向手机上报指定侧的当前附件类型。
 * @param[in] attachment_type `JYT_ATTACHMENT_TYPE` 定义的附件类型。
 * @param[in] attachment_side 附件所在的主从侧身份。
 * @return `true` 表示上报成功，`false` 表示附件类型、侧身份无效或上报失败。
 */
bool system_report_attachment_type(uint8_t attachment_type,
                                   system_attachment_side_t attachment_side) {
    if (attachment_type > JYT_ACC_SPEAKER) {
        floatair_err("reject invalid attachment type: %u", (unsigned)attachment_type);
        return false;
    }
    if ((unsigned)attachment_side >= SYSTEM_ATTACHMENT_SIDE_COUNT) {
        floatair_err("reject invalid attachment side: %u", (unsigned)attachment_side);
        return false;
    }

    floatair_info("report attachment type %u side %u",
                  (unsigned)attachment_type,
                  (unsigned)attachment_side);

    msg_pack_t msgpack = {0};
    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_SYSTEM;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, "SystemInd", sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onAttachmentTypeChanged", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    msg_pack_writer_t* writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 2);
    mpack_write_cstr(&writer->writer, "attachmentType");
    mpack_write_u8(&writer->writer, attachment_type);
    mpack_write_cstr(&writer->writer, "attachmentSide");
    mpack_write_u8(&writer->writer, (uint8_t)attachment_side);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}
