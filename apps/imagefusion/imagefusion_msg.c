/**
 * @file imagefusion_msg.c
 * @brief Imagefusion 手机端参数读写消息处理。
 * @author jytek
 * @version 1.0.0
 * @date 2026-06-16
 * @copyright JYTek
 * @ingroup app_imagefusion
 */
#include "imagefusion.h"

#include "elf_common.h"
#include "floatair_dbg.h"
#include "system/system_runtime_ui.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define IMAGEFUSION_FUSION_MODE_READ 0U   ///< 读取双屏融合偏移参数。
#define IMAGEFUSION_FUSION_MODE_WRITE 1U  ///< 写入双屏融合偏移参数。

/**
 * @brief 向手机端回复当前双屏融合偏移参数。
 *
 * @param msg 原始消息头信息。
 * @param para 双屏融合偏移参数。
 * @return `true` 表示发送成功，`false` 表示发送失败。
 */
static bool imagefusion_send_params_ack(msg_pack_t* msg, const FUSION_PARA_T* para) {
    msg_pack_writer_t* writer = NULL;

    floatair_assert(msg != NULL, "msg is NULL");
    floatair_assert(para != NULL, "para is NULL");

    writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer != NULL, "writer != NULL");

    mpack_start_map(&writer->writer, 4);
    mpack_write_cstr(&writer->writer, "lX");
    mpack_write_i8(&writer->writer, para->left_offset_x);
    mpack_write_cstr(&writer->writer, "lY");
    mpack_write_i8(&writer->writer, para->left_offset_y);
    mpack_write_cstr(&writer->writer, "rX");
    mpack_write_i8(&writer->writer, para->right_offset_x);
    mpack_write_cstr(&writer->writer, "rY");
    mpack_write_i8(&writer->writer, para->right_offset_y);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

/**
 * @brief 向手机端回复 data 为 nil 的成功 ACK。
 *
 * @param msg 原始消息头信息。
 * @return `true` 表示发送成功，`false` 表示发送失败。
 */
static bool imagefusion_send_nil_ack(msg_pack_t* msg) {
    msg_pack_writer_t* writer = NULL;

    floatair_assert(msg != NULL, "msg is NULL");

    writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer != NULL, "writer != NULL");
    mpack_write_nil(&writer->writer);
    return app_mpack_send_writer(writer);
}

/**
 * @brief 判断偏移量是否处于 int8_t 范围。
 *
 * @param value 待检查数值。
 * @return 范围合法返回 `true`，否则返回 `false`。
 */
static bool imagefusion_offset_valid(int32_t value) {
    return value >= INT8_MIN && value <= INT8_MAX;
}

/**
 * @brief 从手机端 data 中解析双屏融合偏移参数。
 *
 * @param node mpack 数据节点。
 * @param para 输出双屏融合偏移参数。
 * @return 解析成功返回 `true`，否则返回 `false`。
 */
static bool imagefusion_parse_params(mpack_node_t node, FUSION_PARA_T* para) {
    int32_t left_x = 0;
    int32_t left_y = 0;
    int32_t right_x = 0;
    int32_t right_y = 0;

    floatair_assert(para != NULL, "para is NULL");

    if (!app_msg_get_32(node, false, "lX", &left_x) ||
        !app_msg_get_32(node, false, "lY", &left_y) ||
        !app_msg_get_32(node, false, "rX", &right_x) ||
        !app_msg_get_32(node, false, "rY", &right_y)) {
        return false;
    }
    if (!imagefusion_offset_valid(left_x) ||
        !imagefusion_offset_valid(left_y) ||
        !imagefusion_offset_valid(right_x) ||
        !imagefusion_offset_valid(right_y)) {
        floatair_err("imagefusion fusion params out of int8 range");
        return false;
    }

    para->left_offset_x = (int8_t)left_x;
    para->left_offset_y = (int8_t)left_y;
    para->right_offset_x = (int8_t)right_x;
    para->right_offset_y = (int8_t)right_y;
    return true;
}

/**
 * @brief 处理读取双屏融合偏移参数命令。
 *
 * @param node mpack 数据节点；当前未使用。
 * @param msg 原始消息头信息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
static bool imagefusion_get_params(mpack_node_t node, msg_pack_t* msg) {
    FUSION_PARA_T para = {0};

    (void)node;
    floatair_assert(msg != NULL, "msg is NULL");

    if (jyt_lr_xy_ctrl(IMAGEFUSION_FUSION_MODE_READ, &para) != 0) {
        floatair_err("imagefusion read fusion params failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    return imagefusion_send_params_ack(msg, &para);
}

/**
 * @brief 处理写入双屏融合偏移参数命令。
 *
 * @param node mpack 数据节点。
 * @param msg 原始消息头信息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
static bool imagefusion_set_params(mpack_node_t node, msg_pack_t* msg) {
    FUSION_PARA_T para = {0};

    floatair_assert(msg != NULL, "msg is NULL");

    if (!imagefusion_parse_params(node, &para)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (jyt_lr_xy_ctrl(IMAGEFUSION_FUSION_MODE_WRITE, &para) != 0) {
        floatair_err("imagefusion write fusion params failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    // system_ui_refresh_screen_now();
    return imagefusion_send_nil_ack(msg);
}

/**
 * @brief Imagefusion 命令路由表。
 */
static app_cmd_func_t s_imagefusion_cmd_funcs[] = {
    {"getFusionParams", imagefusion_get_params},
    {"setFusionParams", imagefusion_set_params},
};

bool imagefusion_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    size_t cmd_count = sizeof(s_imagefusion_cmd_funcs) / sizeof(s_imagefusion_cmd_funcs[0]);

    if (msg == NULL) {
        floatair_err("msg is NULL");
        return false;
    }
    for (size_t index = 0; index < cmd_count; index++) {
        if (strcmp(msg->cmd, s_imagefusion_cmd_funcs[index].cmd) == 0) {
            return s_imagefusion_cmd_funcs[index].func(node, msg);
        }
    }

    floatair_err("unknown imagefusion cmd: %s", msg->cmd);
    return app_mpack_send_ack(msg, ErrCmdErr);
}
