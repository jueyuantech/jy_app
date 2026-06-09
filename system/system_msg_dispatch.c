/**
 * @file system_msg_dispatch.c
 * @brief System message dispatching
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

#ifdef __cplusplus
extern "C" {
#endif
extern app_cmd_func_t system_deviceinfo_cmd_funcs[];
extern app_cmd_func_t system_systemconfig_cmd_funcs[];
extern app_cmd_func_t system_systemstatus_cmd_funcs[];
extern app_cmd_func_t system_systemcontrol_cmd_funcs[];
extern app_cmd_func_t system_systemind_cmd_funcs[];
extern app_cmd_func_t system_notification_cmd_funcs[];
extern app_cmd_func_t system_file_cmd_funcs[];
extern const size_t system_deviceinfo_cmd_funcs_count;
extern const size_t system_systemconfig_cmd_funcs_count;
extern const size_t system_systemstatus_cmd_funcs_count;
extern const size_t system_systemcontrol_cmd_funcs_count;
extern const size_t system_systemind_cmd_funcs_count;
extern const size_t system_notification_cmd_funcs_count;
extern const size_t system_file_cmd_funcs_count;
#ifdef __cplusplus
}
#endif

static bool dispatch_cmd(app_cmd_func_t* funcs, size_t count, mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(funcs != NULL, "funcs is NULL");
    floatair_assert(msg != NULL, "msg is NULL");
    floatair_assert(msg->cmd != NULL, "cmd is NULL");
    for (size_t i = 0; i < count; i++) {
        if (strcmp(msg->cmd, funcs[i].cmd) == 0) {
            floatair_info("find cmd: %s", funcs[i].cmd);
            return funcs[i].func(node, msg);
        }
    }
    floatair_err("unknown cmd: %s", msg->cmd);
    return app_mpack_send_ack(msg, ErrCmdErr);
}

bool system_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    if (strcmp(msg->biz, "DeviceInfo") == 0) {
        return dispatch_cmd(
            system_deviceinfo_cmd_funcs, system_deviceinfo_cmd_funcs_count, node, msg);
    }
    if (strcmp(msg->biz, "SystemConfig") == 0) {
        return dispatch_cmd(
            system_systemconfig_cmd_funcs, system_systemconfig_cmd_funcs_count, node, msg);
    }
    if (strcmp(msg->biz, "SystemStatus") == 0) {
        return dispatch_cmd(
            system_systemstatus_cmd_funcs, system_systemstatus_cmd_funcs_count, node, msg);
    }
    if (strcmp(msg->biz, "SystemControl") == 0) {
        return dispatch_cmd(
            system_systemcontrol_cmd_funcs, system_systemcontrol_cmd_funcs_count, node, msg);
    }
    if (strcmp(msg->biz, "SystemInd") == 0) {
        return dispatch_cmd(
            system_systemind_cmd_funcs, system_systemind_cmd_funcs_count, node, msg);
    }
    if (strcmp(msg->biz, "Notification") == 0) {
        return dispatch_cmd(
            system_notification_cmd_funcs, system_notification_cmd_funcs_count, node, msg);
    }
    if (strcmp(msg->biz, "File") == 0) {
        return dispatch_cmd(system_file_cmd_funcs, system_file_cmd_funcs_count, node, msg);
    }
    floatair_err("unknown biz: %s", msg->biz);
    return app_mpack_send_ack(msg, ErrCmdErr);
}
