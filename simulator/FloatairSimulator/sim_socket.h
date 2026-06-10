/**
 * @brief 模拟器 socket 通信接口。
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "elf_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 模拟器显示展开方式。
 */
typedef enum {
    SIM_SOCKET_DISPLAY_HORIZONTAL = 0, ///< 横开，左右眼按左右并排。
    SIM_SOCKET_DISPLAY_VERTICAL = 1,   ///< 竖开，左右眼按上下堆叠。
    SIM_SOCKET_DISPLAY_SINGLE = 2,     ///< 单开，只显示一个单眼物理区域。
} sim_socket_display_mode_t;

/**
 * @brief 模拟器配置文件内容。
 */
typedef struct {
    char host[256];                         ///< TCP 目标地址。
    uint16_t port;                          ///< TCP 目标端口。
    sim_socket_display_mode_t display_mode; ///< 显示展开方式。
} sim_socket_config_t;

int sim_socket_rx_init(uint16_t port);
int sim_socket_rx_wait(JYT_ELF_MQ_MSG** out_msg, int timeout_ms);
void sim_socket_rx_close(void);

/**
 * @brief 读取模拟器配置文件。
 * @param[out] config 输出配置。
 * @return `0` 表示读取成功或使用默认值，负值表示参数无效。
 */
int sim_socket_config_load(sim_socket_config_t* config);

/**
 * @brief 获取显示展开方式名称。
 * @param[in] mode 显示展开方式。
 * @return 返回只读名称字符串。
 */
const char* sim_socket_display_mode_name(sim_socket_display_mode_t mode);

/**
 * @brief 使用指定目标地址初始化模拟器发送 socket。
 * @param[in] host 目标主机 IP 或域名；传空时沿用默认值。
 * @param[in] port 目标端口；传 0 时沿用默认值。
 * @param[in] side_role 预留的屏幕侧角色参数，当前 Linux 模拟器未使用。
 * @return `0` 表示初始化成功，负值表示初始化失败。
 */
int sim_socket_tx_init(const char* host, uint16_t port, int side_role);

/**
 * @brief 从模拟器配置文件读取目标地址并初始化发送 socket。
 * @param[in] side_role 预留的屏幕侧角色参数，当前 Linux 模拟器未使用。
 * @return `0` 表示初始化成功，负值表示初始化失败。
 */
int sim_socket_tx_init_from_config(int side_role);

int sim_socket_tx_send_chunks(const void* data, size_t size);
int sim_socket_tx_send_raw(const void* data, size_t size);
void sim_socket_tx_close(void);
/* Encode JYT_ELF_MQ_MSG to wire buffer (short format 0xAB + len + payload) */
int sim_socket_encode_mpack(const JYT_ELF_MQ_MSG* in_msg, unsigned char** out_buf, size_t* out_len);
/* Get connection status */
int sim_socket_get_connection_status(void);
/* Get server information */
const char* sim_socket_get_server_info(void);

#ifdef __cplusplus
}
#endif
