#include <time.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "app_def.h"
#include "message.h"
#include "ota.h"
#include "../system/system_res.h"
#include <string.h>

static bool ota_updateprogress(mpack_node_t node, msg_pack_t* msg) {
    int32_t progress = 0;
    if (!app_msg_get_32(node, true, "progress", &progress)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (progress < 0 || progress > 100) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    ota_update_progress((uint8_t)progress);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool ota_success(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    ota_update_progress(100);
    floatair_info("OTA update success");
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool ota_fail(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_info("OTA update fail");
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static app_cmd_func_t ota_cmd_funcs[] = {
    {"progress", ota_updateprogress},
    {"success", ota_success},
    {"fail", ota_fail},
};

static int ota_cmd_funcs_count = sizeof(ota_cmd_funcs) / sizeof(ota_cmd_funcs[0]);

bool ota_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) {
        return false;
    }
    for (int i = 0; i < ota_cmd_funcs_count; i++) {
        if (strcmp(msg->cmd, ota_cmd_funcs[i].cmd) == 0) {
            return ota_cmd_funcs[i].func(node, msg);
        }
    }
    return app_mpack_send_ack(msg, ErrCmdErr);
}
