/**
 * @file system_msg_ack.c
 * @brief 集中处理手机 ACK/NCK 回包，未登记的回包静默结束。
 */
#include "system/system.h"
#include "common/widgets/toast.h"
#include "floatair_dbg.h"

#include <string.h>

/** @brief 手机 ACK/NCK 命令及对应处理函数。 */
typedef struct {
    uint32_t id;      ///< 消息所属模块 ID。
    const char* biz;  ///< 业务名称。
    const char* cmd;  ///< 回包命令名称。
    bool (*handler)(mpack_node_t node, msg_pack_t* msg); ///< 回包处理函数，NULL 表示静默消费。
} system_ack_dispatch_entry_t;

/**
 * @brief 处理关键词唤醒上报的 ACK/NCK 回包。
 * @param[in] node 回包 data 节点，当前不使用错误码及文案。
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

    system_report_kws_hit_response_finish();
    toast_show(app_get_str("TOAST_KWS_PHONE_NO_RESPONSE"));
    return true;
}

/** @brief 需要专门处理的 ACK/NCK 命令表。 */
static const system_ack_dispatch_entry_t s_ack_dispatch_table[] = {
    {APP_MSG_ID_SYSTEM, "SystemInd", "onKeywordSpotting", system_systemind_onkeywordspotting},
};

/**
 * @brief 按命令表处理手机 ACK/NCK，未登记的回包静默结束。
 * @param[in] node 回包 data 节点。
 * @param[in] msg 已解析的回包消息头。
 * @return `true` 表示回包已消费，`false` 表示参数或处理失败。
 */
bool system_handle_ack(mpack_node_t node, msg_pack_t* msg) {
    if (msg == NULL || (msg->type != MSG_TYPE_ACK && msg->type != MSG_TYPE_NAK)) {
        return false;
    }

    for (size_t i = 0; i < sizeof(s_ack_dispatch_table) / sizeof(s_ack_dispatch_table[0]); i++) {
        const system_ack_dispatch_entry_t* entry = &s_ack_dispatch_table[i];
        if (msg->id == entry->id &&
            strcmp(msg->biz, entry->biz) == 0 &&
            strcmp(msg->cmd, entry->cmd) == 0) {
            return entry->handler != NULL ? entry->handler(node, msg) : true;
        }
    }
    return true;
}
