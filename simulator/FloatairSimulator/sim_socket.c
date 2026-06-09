#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include "elf_common.h"
#include "floatair_dbg.h"
#include "sim_socket.h"
#include "simulator_platform.h"
#include "sys_adapter.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 24680
#define RECONNECT_INTERVAL_MS 2000
#define SIMULATOR_SOCKET_CONFIG_FILE "simulator_socket.conf"

static int s_client = -1;
static simulator_socket_endpoint_t server_endpoint;
static char server_host[256] = DEFAULT_HOST;
static uint16_t server_port = DEFAULT_PORT;

static pthread_t retry_thread = 0;
static int retry_thread_running = 0;
static int socket_connected = 0;
static pthread_mutex_t socket_mutex = PTHREAD_MUTEX_INITIALIZER;

static int parse_msg(const unsigned char* buf, size_t len, JYT_ELF_MQ_MSG** out_msg);

/**
 * @brief 将 IPv4 字符串转换为网络字节序地址。
 * @param[in] host IPv4 地址字符串。
 * @param[out] address 输出 IPv4 地址。
 * @return `0` 表示成功，负值表示地址格式无效。
 */
static int sim_socket_parse_ipv4_address(const char* host, struct in_addr* address) {
#if defined(_WIN32)
    unsigned int octets[4] = {0};
    char tail = '\0';

    if (host == NULL || address == NULL) {
        return -1;
    }

    if (sscanf(host,
               "%u.%u.%u.%u%c",
               &octets[0],
               &octets[1],
               &octets[2],
               &octets[3],
               &tail) != 4) {
        return -1;
    }

    if (octets[0] > 255u || octets[1] > 255u || octets[2] > 255u || octets[3] > 255u) {
        return -1;
    }

    address->s_addr = htonl((octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3]);
    return 0;
#else
    if (host == NULL || address == NULL) {
        return -1;
    }

    return inet_pton(AF_INET, host, address) > 0 ? 0 : -1;
#endif
}

/**
 * @brief 去掉配置行首尾空白。
 * @param[in,out] text 待处理字符串。
 * @return 返回去掉首尾空白后的字符串指针。
 */
static char* sim_socket_trim_line(char* text) {
    char* end = NULL;

    if (text == NULL) {
        return NULL;
    }

    text[strcspn(text, "\r\n")] = '\0';
    while (*text == ' ' || *text == '\t') {
        text++;
    }

    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    *end = '\0';
    return text;
}

/**
 * @brief 解析模拟器显示展开方式。
 * @param[in] text 配置文本。
 * @param[out] mode 输出显示展开方式。
 * @return `0` 表示解析成功，负值表示无法识别。
 */
static int sim_socket_parse_display_mode(const char* text, sim_socket_display_mode_t* mode) {
    if (text == NULL || mode == NULL || text[0] == '\0') {
        return -1;
    }

    if (strcmp(text, "0") == 0) {
        *mode = SIM_SOCKET_DISPLAY_HORIZONTAL;
        return 0;
    }

    if (strcmp(text, "1") == 0) {
        *mode = SIM_SOCKET_DISPLAY_VERTICAL;
        return 0;
    }

    if (strcmp(text, "2") == 0) {
        *mode = SIM_SOCKET_DISPLAY_SINGLE;
        return 0;
    }

    return -1;
}

const char* sim_socket_display_mode_name(sim_socket_display_mode_t mode) {
    switch (mode) {
        case SIM_SOCKET_DISPLAY_SINGLE:
            return "single";
        case SIM_SOCKET_DISPLAY_VERTICAL:
            return "vertical";
        case SIM_SOCKET_DISPLAY_HORIZONTAL:
            return "horizontal";
        default:
            return "unknown";
    }
}

int sim_socket_config_load(sim_socket_config_t* config) {
    FILE* fp = NULL;
    char line[64] = {0};
    char exe_dir[512] = {0};
    char config_path[768] = {0};

    if (config == NULL) {
        return -1;
    }

    memset(config, 0, sizeof(*config));
    strncpy(config->host, DEFAULT_HOST, sizeof(config->host) - 1);
    config->port = DEFAULT_PORT;
    config->display_mode = SIM_SOCKET_DISPLAY_VERTICAL;

    simulator_platform_get_executable_dir(exe_dir, sizeof(exe_dir));
    if (exe_dir[0] != '\0') {
        snprintf(config_path, sizeof(config_path), "%s/%s", exe_dir, SIMULATOR_SOCKET_CONFIG_FILE);
        fp = fopen(config_path, "rb");
    }

    if (fp == NULL) {
        fp = fopen(SIMULATOR_SOCKET_CONFIG_FILE, "rb");
        if (fp != NULL) {
            snprintf(config_path, sizeof(config_path), "%s", SIMULATOR_SOCKET_CONFIG_FILE);
        }
    }

    if (fp == NULL) {
        floatair_warn("simulator config not found: %s, use defaults", SIMULATOR_SOCKET_CONFIG_FILE);
        return 0;
    }

    floatair_info("simulator config file: %s", config_path[0] != '\0' ? config_path : SIMULATOR_SOCKET_CONFIG_FILE);

    if (fgets(config->host, sizeof(config->host), fp)) {
        char* host = sim_socket_trim_line(config->host);
        if (host != config->host) {
            memmove(config->host, host, strlen(host) + 1u);
        }
    }

    if (fgets(line, sizeof(line), fp)) {
        char* port_line = sim_socket_trim_line(line);
        int port = atoi(port_line);
        if (port > 0) {
            config->port = (uint16_t)port;
        }
    }

    if (fgets(line, sizeof(line), fp)) {
        char* mode_line = sim_socket_trim_line(line);
        sim_socket_display_mode_t mode = SIM_SOCKET_DISPLAY_VERTICAL;
        if (sim_socket_parse_display_mode(mode_line, &mode) == 0) {
            config->display_mode = mode;
            floatair_info("simulator display mode config: raw=%s parsed=%d(%s)",
                          mode_line,
                          (int)mode,
                          sim_socket_display_mode_name(mode));
        } else if (mode_line[0] != '\0') {
            floatair_warn("invalid simulator display mode '%s', use vertical", mode_line);
        }
    } else {
        floatair_info("simulator display mode config missing, use 1(vertical)");
    }

    fclose(fp);
    return 0;
}

static int sim_socket_prepare_endpoint(simulator_socket_endpoint_t* endpoint, const char* host, uint16_t port) {
    struct sockaddr_in* address = NULL;

    if (!endpoint || !host) {
        return -1;
    }

    memset(endpoint, 0, sizeof(*endpoint));
    address = (struct sockaddr_in*)endpoint->storage;
    address->sin_family = AF_INET;
    address->sin_port = htons(port);
    if (sim_socket_parse_ipv4_address(host, &address->sin_addr) != 0) {
        return -1;
    }

    endpoint->length = (int)sizeof(*address);
    return 0;
}

static void sim_socket_set_connection_state(int connected) {
    uint16_t event_type = 0;
    int changed = 0;

    pthread_mutex_lock(&socket_mutex);
    changed = (socket_connected != connected);
    socket_connected = connected;
    pthread_mutex_unlock(&socket_mutex);

    if (!changed) {
        return;
    }

    event_type = connected ? SET_JYP_HOST_CONNECTED : SET_JYP_HOST_DISCONNECTED;
    simulator_post_system_event(event_type);
}

static void* socket_reconnect_thread(void* arg) {
    (void)arg;

    while (retry_thread_running) {
        int connected = 0;

        pthread_mutex_lock(&socket_mutex);
        connected = socket_connected;
        pthread_mutex_unlock(&socket_mutex);

        if (!connected) {
            int sock = -1;

            floatair_info("Attempting to reconnect to %s:%d...", server_host, server_port);
            if (simulator_platform_socket_open(&server_endpoint, RECONNECT_INTERVAL_MS, &sock) == 0) {
                floatair_info("Connected to host!");
                pthread_mutex_lock(&socket_mutex);
                simulator_platform_socket_close(&s_client);
                s_client = sock;
                pthread_mutex_unlock(&socket_mutex);
                sim_socket_set_connection_state(1);
            } else {
                simulator_platform_socket_close(&sock);
                simulator_platform_sleep_ms(RECONNECT_INTERVAL_MS);
            }
        } else {
            simulator_platform_sleep_ms(1000);
        }
    }

    return NULL;
}

int sim_socket_tx_init(const char* host, uint16_t port, int side_role) {
    (void)side_role;

    if (simulator_platform_socket_global_init() != 0) {
        floatair_err("socket platform init failed");
        return -1;
    }

    pthread_mutex_lock(&socket_mutex);
    if (host && host[0] != '\0') {
        strncpy(server_host, host, sizeof(server_host) - 1);
        server_host[sizeof(server_host) - 1] = '\0';
    }
    if (port > 0) {
        server_port = port;
    }

    if (sim_socket_prepare_endpoint(&server_endpoint, server_host, server_port) != 0) {
        pthread_mutex_unlock(&socket_mutex);
        floatair_err("Invalid address: %s", server_host);
        return -1;
    }

    if (retry_thread_running) {
        pthread_mutex_unlock(&socket_mutex);
        return 0;
    }

    retry_thread_running = 1;
    pthread_mutex_unlock(&socket_mutex);

    if (pthread_create(&retry_thread, NULL, socket_reconnect_thread, NULL) != 0) {
        pthread_mutex_lock(&socket_mutex);
        retry_thread_running = 0;
        pthread_mutex_unlock(&socket_mutex);
        floatair_err("Failed to create reconnect thread");
        return -1;
    }

    return 0;
}

int sim_socket_tx_init_from_config(int side_role) {
    sim_socket_config_t config;

    if (sim_socket_config_load(&config) != 0) {
        return -1;
    }

    floatair_info("Simulator socket target: %s:%u display=%s",
                  config.host,
                  (unsigned)config.port,
                  sim_socket_display_mode_name(config.display_mode));
    return sim_socket_tx_init(config.host, config.port, side_role);
}

static int send_all(int sock, const void* buf, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        int r = simulator_platform_socket_send(sock, (const char*)buf + sent, len - sent);
        if (r <= 0) {
            return -1;
        }
        sent += (size_t)r;
    }

    return (int)sent;
}

int sim_socket_tx_send_raw(const void* data, size_t size) {
    unsigned char len_be[4];
    unsigned int len = 0;
    int sock = -1;
    int connected = 0;
    int r = 0;

    pthread_mutex_lock(&socket_mutex);
    sock = s_client;
    connected = socket_connected;
    pthread_mutex_unlock(&socket_mutex);

    if (!connected || sock < 0) {
        return -1;
    }

    len = (unsigned int)size;
    len_be[0] = (unsigned char)((len >> 24) & 0xFF);
    len_be[1] = (unsigned char)((len >> 16) & 0xFF);
    len_be[2] = (unsigned char)((len >> 8) & 0xFF);
    len_be[3] = (unsigned char)(len & 0xFF);

    r = send_all(sock, len_be, 4);
    if (r < 0) {
        floatair_err("send length failed: %s", strerror(errno));
        pthread_mutex_lock(&socket_mutex);
        simulator_platform_socket_close(&s_client);
        pthread_mutex_unlock(&socket_mutex);
        sim_socket_set_connection_state(0);
        return -1;
    }

    r = send_all(sock, data, size);
    if (r < 0) {
        floatair_err("send data failed: %s", strerror(errno));
        pthread_mutex_lock(&socket_mutex);
        simulator_platform_socket_close(&s_client);
        pthread_mutex_unlock(&socket_mutex);
        sim_socket_set_connection_state(0);
        return -1;
    }

    return (int)size;
}

static int recv_all(int sock, void* buf, size_t len) {
    size_t received = 0;

    while (received < len) {
        int r = simulator_platform_socket_recv(sock, (char*)buf + received, len - received);
        if (r <= 0) {
            return -1;
        }
        received += (size_t)r;
    }

    return (int)received;
}

int sim_socket_rx_wait(JYT_ELF_MQ_MSG** out_msg, int timeout_ms) {
    int sock = -1;
    int connected = 0;
    int res = 0;

    if (!out_msg) {
        return -1;
    }

    pthread_mutex_lock(&socket_mutex);
    sock = s_client;
    connected = socket_connected;
    pthread_mutex_unlock(&socket_mutex);

    if (!connected || sock < 0) {
        simulator_platform_sleep_ms(10);
        return 0;
    }

    res = simulator_platform_socket_wait_readable(sock, timeout_ms);
    if (res > 0) {
        uint8_t len_be[4];
        unsigned int len = 0;
        unsigned char buf[8192];

        if (recv_all(sock, len_be, 4) < 0) {
            floatair_err("recv length failed: %s", strerror(errno));
            pthread_mutex_lock(&socket_mutex);
            simulator_platform_socket_close(&s_client);
            pthread_mutex_unlock(&socket_mutex);
            sim_socket_set_connection_state(0);
            return -1;
        }

        len = ((unsigned int)len_be[0] << 24) |
              ((unsigned int)len_be[1] << 16) |
              ((unsigned int)len_be[2] << 8) |
              (unsigned int)len_be[3];
        if (len == 0 || len > sizeof(buf)) {
            floatair_err("invalid data length: %u", len);
            return -1;
        }

        if (recv_all(sock, buf, len) < 0) {
            floatair_err("recv data failed: %s", strerror(errno));
            pthread_mutex_lock(&socket_mutex);
            simulator_platform_socket_close(&s_client);
            pthread_mutex_unlock(&socket_mutex);
            sim_socket_set_connection_state(0);
            return -1;
        }

        floatair_info("RX %u bytes from server", len);
        if (parse_msg(buf, (size_t)len, out_msg) == 1) {
            return sizeof(JYT_ELF_MQ_MSG*);
        }
    }

    return 0;
}

static int parse_msg(const unsigned char* buf, size_t len, JYT_ELF_MQ_MSG** out_msg) {
    uint8_t mt = 0;
    uint8_t simple_data = 0;
    uint16_t et = 0;
    uint16_t pl = 0;
    uint8_t short_code = 0;
    uint16_t short_pl = 0;
    int short_ok = 0;
    int extended_ok = 0;
    uint16_t ext_et = 0;
    uint16_t ext_pl = 0;
    const unsigned char* content = NULL;
    uint16_t content_len = 0;

    if (len < 3) {
        return 0;
    }

    short_code = buf[0];
    short_pl = (uint16_t)((buf[1] << 8) | buf[2]);
    short_ok = (len >= 3) && ((size_t)3 + short_pl <= len);

    if (!short_ok && len >= 5 && (buf[0] == 0x00 || buf[0] == 0x01)) {
        ext_et = (uint16_t)((buf[1] << 8) | buf[2]);
        ext_pl = (uint16_t)((buf[3] << 8) | buf[4]);
        extended_ok = ((size_t)5 + ext_pl <= len);
    }

    if (short_ok) {
        if (short_code == 0xAB) {
            mt = (uint8_t)EMT_HOST_MPACK_MSG;
            et = 0;
        } else {
            mt = (uint8_t)EMT_SYSTEM_EVENT_WITH_PAYLOAD;
            et = (uint16_t)short_code;
        }
        pl = short_pl;
        buf += 3;
        len -= 3;
    } else if (extended_ok) {
        mt = (buf[0] == 0x00) ? (uint8_t)EMT_HOST_MPACK_MSG : (uint8_t)EMT_SYSTEM_EVENT_WITH_PAYLOAD;
        et = ext_et;
        pl = ext_pl;
        buf += 5;
        len -= 5;
    } else {
        return 0;
    }

    content = buf;
    content_len = pl;
    if (mt == EMT_HOST_MPACK_MSG) {
        if (pl >= 8) {
            uint32_t mlen = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                            ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
            uint32_t frame_crc = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                                 ((uint32_t)buf[6] << 8) | (uint32_t)buf[7];
            uint32_t remain = (uint32_t)pl - 8u;
            if (mlen <= remain) {
                uint32_t calc_crc = hal_crc32((void*)(buf + 8), (uint16_t)mlen);
                if (calc_crc != frame_crc) {
                    floatair_warn("drop host frame by crc32 mismatch calc=0x%08" PRIX32 " recv=0x%08" PRIX32,
                                  calc_crc,
                                  frame_crc);
                    return 0;
                }
                content = buf + 8;
                content_len = (uint16_t)mlen;
            }
        }
    } else {
        switch (et) {
            case 0x03:
                et = (uint16_t)SET_FORCE_SINGLE_CLICK;
                content_len = 0;
                break;
            case 0x04:
                et = (uint16_t)SET_FORCE_DOUBLE_CLICK;
                content_len = 0;
                break;
            case 0x05:
                et = (uint16_t)SET_FORCE_TRI_CLICK;
                content_len = 0;
                break;
            case 0x09:
                et = (uint16_t)SET_FORCE_LONG_PRESSED;
                content_len = 0;
                break;
            case 0x02:
                if (content_len >= 1 && content) {
                    unsigned char c0 = content[0];
                    if (c0 == 'L' || c0 == 'l') {
                        et = (uint16_t)SET_SLIDE_BACKWORD;
                    } else {
                        et = (uint16_t)SET_SLIDE_FORWARD;
                    }
                } else {
                    et = (uint16_t)SET_SLIDE_FORWARD;
                }
                content_len = 0;
                break;
            default:
                break;
        }
        if (et == (uint16_t)SET_KWS_HIT && content_len >= 1 && content) {
            simple_data = content[0];
            content_len = 0;
        }
    }

    {
        size_t alloc = sizeof(JYT_ELF_MQ_MSG) + (size_t)content_len;
        JYT_ELF_MQ_MSG* msg = (JYT_ELF_MQ_MSG*)malloc(alloc);
        if (!msg) {
            floatair_err("malloc failed for message");
            return -1;
        }

        msg->Header.msg_type = mt;
        msg->Header.simple_data = simple_data;
        msg->Header.event_type = et;
        msg->payload_len = content_len;
        if (content_len) {
            memcpy(msg->payload, content, content_len);
        }

        *out_msg = msg;
    }

    return 1;
}

void sim_socket_tx_close(void) {
    if (retry_thread_running) {
        retry_thread_running = 0;
        pthread_join(retry_thread, NULL);
    }
    simulator_platform_socket_close(&s_client);
    sim_socket_set_connection_state(0);
}

int sim_socket_rx_init(uint16_t port) {
    (void)port;
    return 0;
}

int sim_socket_tx_send_msg(const JYT_ELF_MQ_MSG* msg) {
    unsigned char* buf = NULL;
    size_t len = 0;
    int result = 0;

    if (!msg) {
        return -1;
    }

    if (sim_socket_encode_mpack(msg, &buf, &len) < 0) {
        return -1;
    }

    result = sim_socket_tx_send_raw(buf, len);
    free(buf);
    return result;
}

int sim_socket_tx_send_chunks(const void* data, size_t size) {
    return sim_socket_tx_send_raw(data, size);
}

int sim_socket_encode_mpack(const JYT_ELF_MQ_MSG* in_msg, unsigned char** out_buf, size_t* out_len) {
    size_t total_len = 0;
    unsigned char* buf = NULL;

    if (!in_msg || !out_buf || !out_len) {
        return -1;
    }

    total_len = 3 + in_msg->payload_len;
    buf = (unsigned char*)malloc(total_len);
    if (!buf) {
        return -1;
    }

    buf[0] = 0xAB;
    buf[1] = (in_msg->payload_len >> 8) & 0xFF;
    buf[2] = in_msg->payload_len & 0xFF;
    if (in_msg->payload_len > 0) {
        memcpy(buf + 3, in_msg->payload, in_msg->payload_len);
    }

    *out_buf = buf;
    *out_len = total_len;
    return 0;
}

int sim_socket_get_connection_status(void) {
    int status = 0;

    pthread_mutex_lock(&socket_mutex);
    status = socket_connected;
    pthread_mutex_unlock(&socket_mutex);

    return status;
}

const char* sim_socket_get_server_info(void) {
    static char info[300];
    snprintf(info, sizeof(info), "%s:%d", server_host, server_port);
    return info;
}
