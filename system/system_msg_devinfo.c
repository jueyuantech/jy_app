/**
 * @file system_msg_devinfo.c
 * @brief System device info message handling
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
#include "app_def.h"
#include "system/system.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include "sys_adapter.h"

static bool system_deviceinfo_getall(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");

    mpack_start_map(&writer->writer, 12);

    mpack_write_cstr(&writer->writer, "manufacturer");
    mpack_write_cstr(&writer->writer, system_get_manufacture());

    mpack_write_cstr(&writer->writer, "model");
    mpack_write_cstr(&writer->writer, system_get_model());

    mpack_write_cstr(&writer->writer, "edition");
    mpack_write_cstr(&writer->writer, system_get_edition());

    mpack_write_cstr(&writer->writer, "sn");
    mpack_write_cstr(&writer->writer, system_get_sn());

    mpack_write_cstr(&writer->writer, "ssn");
    mpack_write_cstr(&writer->writer, system_get_ssn());

    mpack_write_cstr(&writer->writer, "btMac");
    mpack_write_cstr(&writer->writer, system_get_btaddr());

    mpack_write_cstr(&writer->writer, "btName");
    mpack_write_cstr(&writer->writer, system_get_btname());

    mpack_write_cstr(&writer->writer, "bleMac");
    mpack_write_cstr(&writer->writer, system_get_bleaddr());

    mpack_write_cstr(&writer->writer, "bleName");
    mpack_write_cstr(&writer->writer, system_get_blename());

    mpack_write_cstr(&writer->writer, "fwVer");
    mpack_write_cstr(&writer->writer, system_get_fwver());

    mpack_write_cstr(&writer->writer, "bthVer");
    mpack_write_cstr(&writer->writer, system_get_bthver());

    mpack_write_cstr(&writer->writer, "protocolVer");
    mpack_write_cstr(&writer->writer, system_get_protocolver());

    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getmanufacturer(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "manufacturer");
    mpack_write_cstr(&writer->writer, system_get_manufacture());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getmodel(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "model");
    mpack_write_cstr(&writer->writer, system_get_model());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getedition(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "edition");
    mpack_write_cstr(&writer->writer, system_get_edition());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getsn(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "sn");
    mpack_write_cstr(&writer->writer, system_get_sn());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getbtmac(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "btMac");
    mpack_write_cstr(&writer->writer, system_get_btaddr());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getbtname(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "btName");
    mpack_write_cstr(&writer->writer, system_get_btname());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getblemac(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "bleMac");
    mpack_write_cstr(&writer->writer, system_get_bleaddr());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getblename(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "bleName");
    mpack_write_cstr(&writer->writer, system_get_blename());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getfwver(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "fwVer");
    mpack_write_cstr(&writer->writer, system_get_fwver());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getbthver(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "bthVer");
    mpack_write_cstr(&writer->writer, system_get_bthver());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_deviceinfo_getprotocolver(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "protocolVer");
    mpack_write_cstr(&writer->writer, system_get_protocolver());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

app_cmd_func_t system_deviceinfo_cmd_funcs[] = {
    {"getAll", system_deviceinfo_getall},
    {"getManufacturer", system_deviceinfo_getmanufacturer},
    {"getModel", system_deviceinfo_getmodel},
    {"getEdition", system_deviceinfo_getedition},
    {"getSn", system_deviceinfo_getsn},
    {"getBtMac", system_deviceinfo_getbtmac},
    {"getBtName", system_deviceinfo_getbtname},
    {"getBleMac", system_deviceinfo_getblemac},
    {"getBleName", system_deviceinfo_getblename},
    {"getFwVer", system_deviceinfo_getfwver},
    {"getBthVer", system_deviceinfo_getbthver},
    {"getProtocolVer", system_deviceinfo_getprotocolver}};
const size_t system_deviceinfo_cmd_funcs_count =
    sizeof(system_deviceinfo_cmd_funcs) / sizeof(system_deviceinfo_cmd_funcs[0]);
