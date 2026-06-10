/**
 * @file simulator_event_fifo.c
 * @brief 模拟器系统事件 FIFO 输入实现
 */
#include "simulator_event_fifo.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "elf_common.h"
#include "floatair_dbg.h"
#include "sim_socket.h"
#include "simulator_platform.h"
#include "sys_adapter.h"

/**
 * @brief 文本命令与系统事件类型映射表。
 */
typedef struct {
    const char* name;    ///< FIFO 中接收的事件名称。
    uint16_t event_type; ///< 对应系统事件类型。
    uint8_t param_mode;  ///< 参数编码方式。
    uint32_t fixed_value; ///< 固定参数值。
} simulator_fifo_event_map_t;

enum {
    SIM_FIFO_PARAM_NONE = 0,
    SIM_FIFO_PARAM_SIMPLE_FIXED_U8,
    SIM_FIFO_PARAM_SIMPLE_U8,
    SIM_FIFO_PARAM_PAYLOAD_U8,
    SIM_FIFO_PARAM_PAYLOAD_U32,
    SIM_FIFO_PARAM_BAT_STATUS,
    SIM_FIFO_PARAM_BAT_SOC_ONLY,
    SIM_FIFO_PARAM_CHARGER_FIXED,
    SIM_FIFO_PARAM_DEVICE_STATE_NOW,
    SIM_FIFO_PARAM_DEVICE_STATE_EPOCH,
    SIM_FIFO_PARAM_CALL_STATE_TEXT,
};

#define SIM_CALL_EVENT_RINGING 0
#define SIM_CALL_EVENT_CONNECTED 1
#define SIM_CALL_EVENT_DISCONNECTED 2

static const simulator_fifo_event_map_t g_simulator_fifo_events[] = {
    {"SET_JYP_HOST_CONNECTED", SET_JYP_HOST_CONNECTED, SIM_FIFO_PARAM_NONE, 0},
    {"SET_JYP_HOST_DISCONNECTED", SET_JYP_HOST_DISCONNECTED, SIM_FIFO_PARAM_NONE, 0},
    {"SET_IED_WEAR_ON", SET_IED_WEAR_ON, SIM_FIFO_PARAM_NONE, 0},
    {"SET_IED_REMOVED", SET_IED_REMOVED, SIM_FIFO_PARAM_NONE, 0},
    {"SET_FORCE_SINGLE_CLICK", SET_FORCE_SINGLE_CLICK, SIM_FIFO_PARAM_NONE, 0},
    {"SET_FORCE_DOUBLE_CLICK", SET_FORCE_DOUBLE_CLICK, SIM_FIFO_PARAM_NONE, 0},
    {"SET_FORCE_TRI_CLICK", SET_FORCE_TRI_CLICK, SIM_FIFO_PARAM_NONE, 0},
    {"SET_FORCE_LONG_PRESSED", SET_FORCE_LONG_PRESSED, SIM_FIFO_PARAM_NONE, 0},
    {"SET_SLIDE_FORWARD", SET_SLIDE_FORWARD, SIM_FIFO_PARAM_NONE, 0},
    {"SET_SLIDE_BACKWORD", SET_SLIDE_BACKWORD, SIM_FIFO_PARAM_NONE, 0},
    {"SET_IMU_SINGLE_TAP", SET_IMU_SINGLE_TAP, SIM_FIFO_PARAM_NONE, 0},
    {"SET_IMU_DOUBLE_TAP", SET_IMU_DOUBLE_TAP, SIM_FIFO_PARAM_NONE, 0},
    {"SET_IMU_TILT_UP", SET_IMU_TILT, SIM_FIFO_PARAM_SIMPLE_FIXED_U8, TILT_DIRECTION_UP},
    {"SET_IMU_TILT_DOWN", SET_IMU_TILT, SIM_FIFO_PARAM_SIMPLE_FIXED_U8, TILT_DIRECTION_DOWN},
    {"SET_TWS_LINK_BROKEN", SET_TWS_LINK_BROKEN, SIM_FIFO_PARAM_NONE, 0},
    {"SET_JYT_LOW_BATTERY_WARNING", SET_JYT_LOW_BATTERY_WARNING, SIM_FIFO_PARAM_NONE, 0},
    {"SET_BAT_STATUS", SET_BAT_VOLT_CHANGED, SIM_FIFO_PARAM_BAT_STATUS, 0},
    {"SET_BAT_SOC", SET_BAT_VOLT_CHANGED, SIM_FIFO_PARAM_BAT_SOC_ONLY, 0},
    {"SET_CHARGER_ON", SET_BAT_VOLT_CHANGED, SIM_FIFO_PARAM_CHARGER_FIXED, 1},
    {"SET_CHARGER_OFF", SET_BAT_VOLT_CHANGED, SIM_FIFO_PARAM_CHARGER_FIXED, 0},
    {"SET_KWS_HIT", SET_KWS_HIT, SIM_FIFO_PARAM_SIMPLE_FIXED_U8, 1},
    {"SET_REPORT_DEVICE_STATE_NOW", SET_REPORT_DEVICE_STATE, SIM_FIFO_PARAM_DEVICE_STATE_NOW, 0},
    {"SET_REPORT_DEVICE_STATE", SET_REPORT_DEVICE_STATE, SIM_FIFO_PARAM_DEVICE_STATE_EPOCH, 0},
    {"SET_BT_CALL_RINGING", SET_BT_CALL_SETUP_EVENT, SIM_FIFO_PARAM_CALL_STATE_TEXT, SIM_CALL_EVENT_RINGING},
    {"SET_BT_CALL_CONNECTED", SET_BT_CALL_SETUP_EVENT, SIM_FIFO_PARAM_CALL_STATE_TEXT, SIM_CALL_EVENT_CONNECTED},
    {"SET_BT_CALL_DISCONNECTED", SET_BT_CALL_SETUP_EVENT, SIM_FIFO_PARAM_CALL_STATE_TEXT, SIM_CALL_EVENT_DISCONNECTED},
    {"SET_JYT_BT_VISIBLE_CHANGED", SET_JYT_BT_VISIBLE_CHANGED, SIM_FIFO_PARAM_PAYLOAD_U8, 0},
    {"SET_JYT_TIMER_TRIGGER", SET_JYT_TIMER_TRIGGER, SIM_FIFO_PARAM_PAYLOAD_U32, 0},
};

static pthread_t g_simulator_event_fifo_thread;
static int g_simulator_event_fifo_started = 0;
static int g_simulator_event_fifo_running = 0;
static int g_simulator_event_fifo_fd = -1;
static uint8_t g_simulator_battery_soc = 80;
static uint8_t g_simulator_charge_state = 0;

/**
 * @brief 按当前缓存电池状态向系统上报一次电池消息。
 * @return 无返回值。
 */
static void simulator_event_fifo_post_battery_status(void) {
    union bat_state_t bat_status = {0};

    bat_status.bat_chg_combo.soc = g_simulator_battery_soc;
    bat_status.bat_chg_combo.charger_mode = g_simulator_charge_state;
    bat_status.bat_chg_combo.voltage_mv = 4200;
    simulator_post_system_event_ex(SET_BAT_VOLT_CHANGED,
                                   0,
                                   &bat_status,
                                   (uint16_t)sizeof(bat_status));
}

/**
 * @brief 处理 FIFO 收到的一行文本命令。
 * @param[in,out] line 一行命令文本，会在函数内原地裁剪换行。
 * @return 无返回值。
 */
static void simulator_event_fifo_handle_line(char* line) {
    size_t i = 0;
    char* arg = NULL;
    unsigned long parsed_value = 0;
    char* end = NULL;

    if (!line) {
        return;
    }

    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0') {
        return;
    }

    arg = line;
    while (*arg && *arg != ' ' && *arg != '\t') {
        arg++;
    }
    if (*arg) {
        *arg++ = '\0';
        while (*arg == ' ' || *arg == '\t') {
            arg++;
        }
        if (*arg == '\0') {
            arg = NULL;
        }
    } else {
        arg = NULL;
    }

    for (i = 0; i < sizeof(g_simulator_fifo_events) / sizeof(g_simulator_fifo_events[0]); ++i) {
        if (strcmp(line, g_simulator_fifo_events[i].name) == 0) {
            floatair_info("fifo event recv: %s(%u)", line, (unsigned)g_simulator_fifo_events[i].event_type);
            if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_NONE) {
                simulator_post_system_event(g_simulator_fifo_events[i].event_type);
                return;
            }

            if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_SIMPLE_FIXED_U8) {
                simulator_post_system_event_ex(g_simulator_fifo_events[i].event_type,
                                               (uint8_t)g_simulator_fifo_events[i].fixed_value,
                                               NULL,
                                               0);
                return;
            }

            if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_BAT_STATUS) {
                char* arg2 = NULL;
                unsigned long soc = 0;
                unsigned long charger_mode = 0;

                if (!arg) {
                    floatair_warn("fifo battery event missing args");
                    return;
                }

                arg2 = arg;
                while (*arg2 && *arg2 != ' ' && *arg2 != '\t') {
                    arg2++;
                }
                if (*arg2) {
                    *arg2++ = '\0';
                    while (*arg2 == ' ' || *arg2 == '\t') {
                        arg2++;
                    }
                    if (*arg2 == '\0') {
                        arg2 = NULL;
                    }
                } else {
                    arg2 = NULL;
                }

                if (!arg2) {
                    floatair_warn("fifo battery event missing charger_mode");
                    return;
                }

                soc = strtoul(arg, &end, 0);
                if (end == arg || *end != '\0' || soc > 100) {
                    floatair_warn("fifo battery event bad soc: %s", arg);
                    return;
                }

                charger_mode = strtoul(arg2, &end, 0);
                if (end == arg2 || *end != '\0' || charger_mode > 255) {
                    floatair_warn("fifo battery event bad charger_mode: %s", arg2);
                    return;
                }

                g_simulator_battery_soc = (uint8_t)soc;
                g_simulator_charge_state = (uint8_t)charger_mode;
                simulator_event_fifo_post_battery_status();
                return;
            }

            if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_BAT_SOC_ONLY) {
                if (!arg) {
                    floatair_warn("fifo battery soc missing arg");
                    return;
                }
                parsed_value = strtoul(arg, &end, 0);
                if (end == arg || *end != '\0' || parsed_value > 100) {
                    floatair_warn("fifo battery soc bad arg: %s", arg);
                    return;
                }
                g_simulator_battery_soc = (uint8_t)parsed_value;
                simulator_event_fifo_post_battery_status();
                return;
            }

            if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_CHARGER_FIXED) {
                g_simulator_charge_state = (uint8_t)g_simulator_fifo_events[i].fixed_value;
                simulator_event_fifo_post_battery_status();
                return;
            }

            if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_DEVICE_STATE_NOW ||
                g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_DEVICE_STATE_EPOCH) {
                jyt_device_state_t device_state = {0};
                device_state.host_connected = sim_socket_get_connection_status() ? 1 : 0;

                if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_DEVICE_STATE_NOW) {
                    device_state.time_now = time(NULL);
                } else {
                    if (!arg) {
                        floatair_warn("fifo device state missing epoch");
                        return;
                    }
                    parsed_value = strtoul(arg, &end, 0);
                    if (end == arg || *end != '\0') {
                        floatair_warn("fifo device state bad epoch: %s", arg);
                        return;
                    }
                    device_state.time_now = (time_t)parsed_value;
                }

                simulator_post_system_event_ex(g_simulator_fifo_events[i].event_type,
                                               0,
                                               &device_state,
                                               (uint16_t)sizeof(device_state));
                return;
            }

            if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_CALL_STATE_TEXT) {
                uint8_t payload[96] = {0};
                size_t caller_len = 0;

                payload[0] = (uint8_t)g_simulator_fifo_events[i].fixed_value;
                if (arg && arg[0] != '\0') {
                    caller_len = strlen(arg);
                    if (caller_len > sizeof(payload) - 2) {
                        caller_len = sizeof(payload) - 2;
                    }
                    memcpy(payload + 1, arg, caller_len);
                }

                simulator_post_system_event_ex(g_simulator_fifo_events[i].event_type,
                                               0,
                                               payload,
                                               (uint16_t)(1 + caller_len));
                return;
            }

            if (!arg) {
                floatair_warn("fifo event missing arg: %s", line);
                return;
            }

            parsed_value = strtoul(arg, &end, 0);
            if (end == arg || *end != '\0') {
                floatair_warn("fifo event bad arg: %s %s", line, arg);
                return;
            }

            if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_SIMPLE_U8) {
                simulator_post_system_event_ex(g_simulator_fifo_events[i].event_type,
                                               (uint8_t)parsed_value,
                                               NULL,
                                               0);
            } else if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_PAYLOAD_U8) {
                uint8_t payload_u8 = (uint8_t)parsed_value;
                simulator_post_system_event_ex(g_simulator_fifo_events[i].event_type,
                                               0,
                                               &payload_u8,
                                               (uint16_t)sizeof(payload_u8));
            } else if (g_simulator_fifo_events[i].param_mode == SIM_FIFO_PARAM_PAYLOAD_U32) {
                uint32_t payload_u32 = (uint32_t)parsed_value;
                simulator_post_system_event_ex(g_simulator_fifo_events[i].event_type,
                                               0,
                                               &payload_u32,
                                               (uint16_t)sizeof(payload_u32));
            }
            return;
        }
    }

    floatair_warn("unknown fifo event: %s", line);
}

/**
 * @brief FIFO 监听线程主循环。
 * @param[in] arg 线程参数，当前未使用。
 * @return 线程退出值，固定返回 `NULL`。
 */
static void* simulator_event_fifo_thread_main(void* arg) {
    char read_buf[256];
    char line_buf[512];
    size_t line_len = 0;

    (void)arg;

    while (g_simulator_event_fifo_running) {
        int r = simulator_platform_fifo_read(g_simulator_event_fifo_fd, read_buf, sizeof(read_buf));
        if (r > 0) {
            int i = 0;
            for (i = 0; i < r; ++i) {
                char ch = read_buf[i];
                if (ch == '\n') {
                    line_buf[line_len] = '\0';
                    simulator_event_fifo_handle_line(line_buf);
                    line_len = 0;
                    continue;
                }

                if (line_len + 1 < sizeof(line_buf)) {
                    line_buf[line_len++] = ch;
                }
            }
        } else if (r < 0 && !simulator_platform_fifo_read_would_block()) {
            floatair_warn("fifo read failed: %s", strerror(errno));
            simulator_platform_sleep_ms(20);
        } else {
            simulator_platform_sleep_ms(20);
        }
    }

    return NULL;
}

bool simulator_event_fifo_start(void) {
    const char* fifo_path = simulator_platform_fifo_default_path();

    if (g_simulator_event_fifo_started) {
        return true;
    }

    if (!simulator_platform_fifo_start(fifo_path,
                                       &g_simulator_event_fifo_fd,
                                       &g_simulator_event_fifo_thread,
                                       &g_simulator_event_fifo_running,
                                       simulator_event_fifo_thread_main)) {
        if (g_simulator_event_fifo_fd < 0) {
            floatair_info("simulator event fifo is disabled on this platform");
        } else {
            floatair_err("failed to start simulator event fifo");
        }
        return false;
    }

    g_simulator_event_fifo_started = 1;
    if (g_simulator_event_fifo_fd >= 0) {
        floatair_info("simulator event fifo ready: %s", fifo_path);
    } else {
        floatair_info("simulator event fifo is disabled on this platform");
    }
    return true;
}

void simulator_event_fifo_stop(void) {
    const char* fifo_path = simulator_platform_fifo_default_path();

    if (!g_simulator_event_fifo_started) {
        return;
    }

    simulator_platform_fifo_stop(fifo_path,
                                 &g_simulator_event_fifo_fd,
                                 &g_simulator_event_fifo_thread,
                                 &g_simulator_event_fifo_running);
    g_simulator_event_fifo_started = 0;
}
