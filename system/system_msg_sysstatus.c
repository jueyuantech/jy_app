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
#include "message.h"
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
    uint8_t sys_state = 0;
    if (!app_msg_get_u8(node, false, "sysState", &sys_state)) {
        floatair_err("sysState is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("sysState %d", sys_state);
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

static bool system_systemstatus_getromusage(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 3);
    mpack_write_cstr(&writer->writer, "total");
    mpack_write_u32(&writer->writer, system_get_rom_total());
    mpack_write_cstr(&writer->writer, "used");
    mpack_write_u32(&writer->writer, system_get_rom_used());
    mpack_write_cstr(&writer->writer, "remaining");
    mpack_write_u32(&writer->writer, system_get_rom_remaining());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

app_cmd_func_t system_systemstatus_cmd_funcs[] = {
    {"getAll", system_systemstatus_getall},
    {"getSysState", system_systemstatus_getsysstate},
    {"setSysState", system_systemstatus_setsysstate},
    {"getChargeState", system_systemstatus_getchargestate},
    {"getBattery", system_systemstatus_getbattery},
    {"getRomUsage", system_systemstatus_getromusage}};
const size_t system_systemstatus_cmd_funcs_count =
    sizeof(system_systemstatus_cmd_funcs) / sizeof(system_systemstatus_cmd_funcs[0]);
