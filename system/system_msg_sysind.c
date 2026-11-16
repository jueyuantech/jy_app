/**
 * @file system_msg_sysind.c
 * @brief System indication message handling
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

#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include "sys_adapter.h"


static bool system_systemind_heartbeat(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemind_keepalive(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

app_cmd_func_t system_systemind_cmd_funcs[] = {
    {"heartBeat", system_systemind_heartbeat},
    {"keepAlive", system_systemind_keepalive},
};
const size_t system_systemind_cmd_funcs_count =
    sizeof(system_systemind_cmd_funcs) / sizeof(system_systemind_cmd_funcs[0]);
