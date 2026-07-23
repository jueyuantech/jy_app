/**
 * @file system_msg_deviceconnection.c
 * @brief 设备连接消息的解析与应用配置处理。
 */
#include <time.h>

#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "common/app_framework/app_router.h"

#include <inttypes.h>

/**
 * @brief 根据上位机平台应用连接配置。
 * @param[in] node 消息 payload 数据节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示消息已处理并回包，`false` 表示处理失败。
 */
static bool system_deviceconnection_setappconfig(mpack_node_t node, msg_pack_t* msg) {
    uint32_t app_platform = APP_ROUTER_APP_PLATFORM_NONE;

    floatair_assert(msg != NULL, "msg is NULL");
    if (!app_msg_get_u32(node, false, "appPlatform", &app_platform)) {
        floatair_err("appPlatform is invalid");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    switch (app_platform) {
        case APP_ROUTER_APP_PLATFORM_ANDROID:
        case APP_ROUTER_APP_PLATFORM_IOS:
        case APP_ROUTER_APP_PLATFORM_MACOS:
        case APP_ROUTER_APP_PLATFORM_WINDOWS:
        case APP_ROUTER_APP_PLATFORM_WATCH:
            break;
        default:
            floatair_err("appPlatform out of range: %" PRIu32, app_platform);
            return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_router_apply_app_config(app_platform)) {
        floatair_err("apply app config failed, appPlatform=%" PRIu32, app_platform);
        return app_mpack_send_ack(msg, ErrBizErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

app_cmd_func_t system_deviceconnection_cmd_funcs[] = {
    {"setAppConfig", system_deviceconnection_setappconfig},
};

const size_t system_deviceconnection_cmd_funcs_count =
    sizeof(system_deviceconnection_cmd_funcs) / sizeof(system_deviceconnection_cmd_funcs[0]);
