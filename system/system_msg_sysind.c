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
#include "common/widgets/toast.h"
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

/**
 * @brief 处理关键词唤醒上报的 ACK/NCK 回包。
 *
 * @param[in] node 回包 data 节点，NCK 时包含错误码与错误文案。
 * @param[in] msg 消息上下文。
 * @return `true` 表示回包已处理。
 */
static bool system_systemind_onkeywordspotting(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");

    if (msg->type == MSG_TYPE_ACK) {
        system_report_kws_hit_response_finish();
        return true;
    }

    if (msg->type != MSG_TYPE_NAK) {
        return app_mpack_send_ack(msg, ErrCmdErr);
    }

    system_report_kws_hit_response_finish();
    toast_show(app_get_str("TOAST_KWS_PHONE_NO_RESPONSE"));
    return true;
}

app_cmd_func_t system_systemind_cmd_funcs[] = {
    {"heartBeat", system_systemind_heartbeat},
    {"keepAlive", system_systemind_keepalive},
    {"onKeywordSpotting", system_systemind_onkeywordspotting},
};
const size_t system_systemind_cmd_funcs_count =
    sizeof(system_systemind_cmd_funcs) / sizeof(system_systemind_cmd_funcs[0]);
