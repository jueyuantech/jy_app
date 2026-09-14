/**
 * @file system_msg_sysstatus.c
 * @brief System status message handling
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include <time.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "floatair_fs.h"
#include "message.h"
#include "app_lcd.h"
#include "system/system.h"
#include "sys_adapter.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

static bool system_systemstatus_getall(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 3);
    mpack_write_cstr(&writer->writer, "sysState");
    mpack_write_u8(&writer->writer, system_get_sys_state());
    mpack_write_cstr(&writer->writer, "chargeState");
    mpack_write_u8(&writer->writer, system_get_charge_state());
    mpack_write_cstr(&writer->writer, "battery");
    mpack_write_u8(&writer->writer, system_get_battery());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemstatus_getsysstate(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "sysState");
    mpack_write_u8(&writer->writer, system_get_sys_state());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemstatus_setsysstate(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    uint8_t sys_state = (uint8_t)LCD_OFF;
    if (!app_msg_get_u8(node, false, "sysState", &sys_state)) {
        floatair_err("sysState is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!floatair_lcd_state_is_valid((lcd_state_t)sys_state)) {
        floatair_err("invalid sysState: %u, expected 0(OFF) or 1(ON)",
                     (unsigned)sys_state);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("sysState %u(%s)",
                  (unsigned)sys_state,
                  floatair_lcd_state_name((lcd_state_t)sys_state));
    system_set_sys_state(sys_state);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemstatus_getchargestate(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "chargeState");
    mpack_write_u8(&writer->writer, system_get_charge_state());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemstatus_getbattery(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "battery");
    mpack_write_u8(&writer->writer, system_get_battery());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

/**
 * @brief 获取主从两侧最近一次收到的附件类型快照。
 * @param[in] node 消息数据节点，当前未使用。
 * @param[in] msg 原始消息包，用于回复 ACK。
 * @return `true` 表示回包发送成功，`false` 表示发送失败。
 */
static bool system_systemstatus_getattachmentstate(mpack_node_t node, msg_pack_t* msg) {
    system_attachment_snapshot_t snapshot = {0};
    msg_pack_writer_t* writer = NULL;

    (void)node;
    floatair_assert(msg != NULL, "msg is NULL");
    system_get_attachment_state(&snapshot);

    writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer != NULL, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "attachments");
    mpack_start_array(&writer->writer, SYSTEM_ATTACHMENT_SIDE_COUNT);
    for (uint8_t side = 0; side < SYSTEM_ATTACHMENT_SIDE_COUNT; side++) {
        mpack_start_map(&writer->writer, 2);
        mpack_write_cstr(&writer->writer, "attachmentSide");
        mpack_write_u8(&writer->writer, side);
        mpack_write_cstr(&writer->writer, "attachmentType");
        mpack_write_u8(&writer->writer, snapshot.type_by_side[side]);
        mpack_finish_map(&writer->writer);
    }
    mpack_finish_array(&writer->writer);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemstatus_getromusage(mpack_node_t node, msg_pack_t* msg) {
    floatair_fs_usage_t usage = {0};

    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    if (floatair_fs_get_usage(floatair_fs_get_root_path(), &usage) != FLOATAIR_FS_OK) {
        floatair_err("get lfsd usage failed");
        return app_mpack_send_ack(msg, ErrBizErr);
    }
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 3);
    mpack_write_cstr(&writer->writer, "total");
    mpack_write_u32(&writer->writer, usage.total);
    mpack_write_cstr(&writer->writer, "used");
    mpack_write_u32(&writer->writer, usage.used);
    mpack_write_cstr(&writer->writer, "remaining");
    mpack_write_u32(&writer->writer, usage.remaining);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

app_cmd_func_t system_systemstatus_cmd_funcs[] = {
    {"getAll", system_systemstatus_getall},
    {"getSysState", system_systemstatus_getsysstate},
    {"setSysState", system_systemstatus_setsysstate},
    {"getChargeState", system_systemstatus_getchargestate},
    {"getBattery", system_systemstatus_getbattery},
    {"getAttachmentState", system_systemstatus_getattachmentstate},
    {"getRomUsage", system_systemstatus_getromusage}};
const size_t system_systemstatus_cmd_funcs_count =
    sizeof(system_systemstatus_cmd_funcs) / sizeof(system_systemstatus_cmd_funcs[0]);
