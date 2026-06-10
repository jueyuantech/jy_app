/**
 * @file floatair_run.c
 * @brief 应用运行时入口、系统初始化和消息循环调度实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup common
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 导入 LVGL 头文件和自定义配置 */
#include "floatair_dbg.h"
#include "floatair_osal.h"
#include "floatair_def.h"
#include "message.h"
#include "app_lcd.h"
#include "app_def.h"
#include "common/app_framework/app_stereo.h"
#include "common/elf_common.h"
#include "system/system_timer.h"
#include "mpack.h"
#include "common/app_framework/app_router.h"
#include "system/system_config_json.h"
#include "system/stt_common.h"
#include "system/system.h"

#include <lvgl/lvgl.h>

jyt_section_data_t g_section_data = {0};
bt_info g_bt_info                 = {0};

typedef struct floatair_minute_cb_node {
    floatair_minute_cb_t cb;
    struct floatair_minute_cb_node* next;
} floatair_minute_cb_node_t;

static floatair_minute_cb_node_t* g_minute_cb_head = NULL;

/**
 * @brief 查询当前 Q-8 中仍待消费的消息数量。
 * @return 成功返回待消费消息数量，失败返回 -1。
 */
static int floatair_get_app_msg_queue_pending(void) {
#if defined(BUILD_NATIVE)
    return -1;
#else
    static mqd_t q8_handle = (mqd_t)-1;
    struct mq_attr attr = {0};

    if (q8_handle == (mqd_t)-1) {
        char qname[MQ_NAME_LEN] = {0};

        attr.mq_maxmsg = 64;
        attr.mq_msgsize = sizeof(OSAL_MQ_MSG*);
        attr.mq_flags = 0;
        snprintf(qname, sizeof(qname), "/JYT_MQ_%d", MQ_JYT_ELFAPP_DATA_IN);
        q8_handle = mq_open(qname, O_RDWR | O_CREAT, 0666, &attr);
        if (q8_handle == (mqd_t)-1) {
            floatair_warn("open Q-8 for attr failed: %s", strerror(errno));
            return -1;
        }
    }

    if (mq_getattr(q8_handle, &attr) != 0) {
        floatair_warn("get Q-8 attr failed: %s", strerror(errno));
        return -1;
    }
    return (int)attr.mq_curmsgs;
#endif
}

/**
 * @brief 打印 ELF 队列消息的头信息与前缀字节，便于排查消息是否被错误转发。
 * @param[in] tag 日志标签。
 * @param[in] msg ELF 队列消息指针。
 * @return 无返回值。
 */
static void floatair_log_elf_queue_msg_preview(const char* tag, const JYT_ELF_MQ_MSG* msg) {
    char payload_preview[3 * 16 + 1] = {0};
    size_t preview_len = 0;
    size_t pos = 0;

    if (msg == NULL) {
        floatair_err("%s: msg is NULL", tag ? tag : "elf msg");
        return;
    }

    preview_len = msg->payload_len;
    if (preview_len > 16) {
        preview_len = 16;
    }

    for (size_t i = 0; i < preview_len && pos + 4 < sizeof(payload_preview); i++) {
        int written = snprintf(payload_preview + pos,
                               sizeof(payload_preview) - pos,
                               "%02X ",
                               msg->payload[i]);
        if (written <= 0) {
            break;
        }
        pos += (size_t)written;
    }

    if (pos > 0) {
        payload_preview[pos - 1] = '\0';
    }

    floatair_info("%s: msg_type=%u event_type=%u simple=%u payload_len=%u preview[%zu]=%s",
                  tag ? tag : "elf msg",
                  (unsigned)msg->Header.msg_type,
                  (unsigned)msg->Header.event_type,
                  (unsigned)msg->Header.simple_data,
                  (unsigned)msg->payload_len,
                  preview_len,
                  pos > 0 ? payload_preview : "<empty>");
}

void floatair_register_minute_cb(floatair_minute_cb_t cb) {
    if (cb == NULL) {
        floatair_err("floatair_register_minute_cb: cb is NULL");
        return;
    }

    floatair_minute_cb_node_t* p = g_minute_cb_head;
    while (p != NULL) {
        if (p->cb == cb) {
            floatair_err("floatair_register_minute_cb: cb already registered");
            return;
        }
        p = p->next;
    }

    floatair_minute_cb_node_t* node =
        (floatair_minute_cb_node_t*)malloc(sizeof(floatair_minute_cb_node_t));
    if (node == NULL) {
        floatair_err("malloc failed in floatair_register_minute_cb");
        return;
    }

    node->cb = cb;
    node->next = NULL;

    if (g_minute_cb_head == NULL) {
        g_minute_cb_head = node;
    } else {
        p = g_minute_cb_head;
        while (p->next != NULL) {
            p = p->next;
        }
        p->next = node;
    }
}

static void floatair_call_minute_cbs(void) {
    floatair_minute_cb_node_t* node = g_minute_cb_head;
    while (node != NULL) {
        if (node->cb != NULL) {
            node->cb();
        }
        node = node->next;
    }
}

static void app_msg_recv(void) {
    while (1) {
        OSAL_MQ_MSG *msg = OSAL_WAITING_MQ_MSG(MQ_JYT_ELFAPP_DATA_IN, CPU_SPEED_REQ_FULL);
        if (msg) {
            uint32_t msg_start_time_us = (uint32_t)GetTimeUs();
            JYT_ELF_MQ_MSG* p_que_data = NULL;
            bool handle_ret = false;
            bool refreshed = false;
            uint8_t msg_type = 0;
            uint16_t event_type = 0;
            uint16_t payload_len = 0;
            if (msg->header.id == LMID_ELFMSG_WRAP) {
                p_que_data=(JYT_ELF_MQ_MSG*)(msg->pdu.ptr[0]);
                if (p_que_data) {
                    msg_type = p_que_data->Header.msg_type;
                    event_type = p_que_data->Header.event_type;
                    payload_len = p_que_data->payload_len;
                    floatair_log_elf_queue_msg_preview("recv elf wrap", p_que_data);
                    switch (p_que_data->Header.msg_type) {
                        case EMT_HOST_MPACK_MSG: {
                            int q_pending = floatair_get_app_msg_queue_pending();
                            stt_set_flow_queue_pending(q_pending);
                            app_msg_dump_summary((char*) p_que_data->payload, p_que_data->payload_len, "mpack recv");
                            app_msg_dump((char*) p_que_data->payload, p_que_data->payload_len, "phone msg");
                            handle_ret = app_mpack_msg_handle((char*) p_que_data->payload, p_que_data->payload_len);
                            break;
                        }
                        case EMT_SYSTEM_EVENT: {
                            handle_ret = app_system_msg_handle(p_que_data);
                            break;
                        }
                        case EMT_SYSTEM_EMERG_MSG: {
                            handle_ret = app_emerg_msg_handle((char*) p_que_data->payload, p_que_data->payload_len);
                            break;
                        }
                        case EMT_SYSTEM_EVENT_WITH_PAYLOAD: {
                            handle_ret = app_system_msg_handle_payload(p_que_data);
                            break;
                        }
                        default:
                            floatair_err("----, msg type not support %d", p_que_data->Header.msg_type);
                            break;
                    }
                } else {
                    floatair_err("p_que_data is NULL");
                }
            } else {
                floatair_warn("unexpected mq msg id=%u size=%u in app_msg_recv",
                              (unsigned)msg->header.id,
                              (unsigned)msg->header.pdu_size);
            }
            if (!handle_ret) {
                if (p_que_data != NULL) {
                    floatair_log_elf_queue_msg_preview("handle ret false detail", p_que_data);
                }
                floatair_err("handle ret false");
            } else {
                //floatair_info("handle true, lv_timer_handler()");
                lv_timer_handler(); // 先补刷一个，响应UI变化
                refreshed = true;
            }
            /* The wrapped ELF message is heap-allocated by system_manager and
             * passed through the MQ as a raw pointer, so the consumer must
             * release it after handling. */
            if (p_que_data != NULL) {
                free(p_que_data);
            }
            OSAL_DELETE_MQ_MSG(msg);
            {
                uint32_t msg_cost_us = (uint32_t)GetTimeUs() - msg_start_time_us;
                floatair_info("app msg recv cost %lu us/%lu ms, handle_ret=%d refreshed=%d msg_type=%u event_type=%u payload_len=%u",
                              (unsigned long)msg_cost_us,
                              (unsigned long)(msg_cost_us / 1000U),
                              handle_ret ? 1 : 0,
                              refreshed ? 1 : 0,
                              (unsigned)msg_type,
                              (unsigned)event_type,
                              (unsigned)payload_len);
            }
        }
    }
}

void floatair_load(void) {
    jyt_get_ft_info(&g_section_data);
    system_dump_jyt_section();

    jyt_get_bt_info(&g_bt_info);
    system_dump_bt_info();

    floatair_register_minute_cb(system_update_time);

    app_msg_init();
    if (!system_cfgfile_load()) {
        //floatair_assert(false, "system_cfgfile_load failed");
        floatair_err("system_cfgfile_load failed, wait for debug");
        return;
    }
    if (!system_font_init()) {
        floatair_assert(false, "system_font_init failed");
    }
    // ---------- black full screen at begining -----------
    lv_obj_t* p_root = system_init_lvgl_fb();
    floatair_assert(p_root != NULL, "system_init_lvgl_fb failed");
    floatair_lcd_set_brightness(system_config_get_brightness());

    //------------------------------------------------------------
    if (!app_router_init()) {
        floatair_assert(false, "app_router_init failed");
    }
    system_init();
    floatair_info("-------enter load_app_home");
    bool ret = app_router_call_home();
    floatair_assert(ret, "app_router_call_home failed");

    floatair_info("-------enter app msg recv");
    app_msg_recv();
}

void floatair_unload(void) {
    floatair_info("-------enter app deinit");
    system_deinit();
    app_msg_deinit();
    floatair_info("LVGL demo completed");
}

void floatair_lvgl_tick(void) {
#if 0
    lv_timer_handler();

    static uint32_t tick_count = 0;
    static uint32_t sec_count = 0;
    tick_count++;

    const uint32_t ticks_per_sec = SYSTEM_LVGL_SECOND_PERIOD / SYSTEM_LVGL_TICK_PERIOD;
    const uint32_t ticks_per_min = (SYSTEM_LVGL_SECOND_PERIOD * 60u) / SYSTEM_LVGL_TICK_PERIOD;

    if (ticks_per_sec > 0 && (tick_count % ticks_per_sec) == 0) {
        sec_count++;
        floatair_info("lvgl tick %" PRIu32 " second", sec_count);
    }

    if (ticks_per_min > 0 && (tick_count % ticks_per_min) == 0) {
        floatair_lvgl_period_minute();
    }
#else
    floatair_lvgl_period_minute();
#endif
}

void floatair_lvgl_period_minute(void) {
    floatair_info("lvgl period minute");
    floatair_call_minute_cbs();
}
