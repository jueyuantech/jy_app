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
#include "elf_common.h"
#include "system/system_timer.h"
#include "mpack.h"
#include "common/app_framework/app_router.h"
#include "system/system_config_json.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

#include <lvgl/lvgl.h>
#include <lvgl/src/core/lv_refr_private.h>

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

static void app_msg_handle(OSAL_MQ_MSG* msg) {
    JYT_ELF_MQ_MSG* p_que_data = NULL;
    bool handle_ret = false;

    if (msg == NULL) {
        return;
    }
    if (msg->header.id == LMID_ELFMSG_WRAP) {
        p_que_data = (JYT_ELF_MQ_MSG*)(msg->pdu.ptr[0]);
        if (p_que_data) {
            floatair_log_elf_queue_msg_preview("recv elf wrap", p_que_data);
            switch (p_que_data->Header.msg_type) {
                case EMT_HOST_MPACK_MSG: {
                    int q_pending = floatair_get_app_msg_queue_pending();
                    stt_set_flow_queue_pending(q_pending);
                    app_msg_dump_summary((char*)p_que_data->payload,
                                         p_que_data->payload_len,
                                         "mpack recv");
                    app_msg_dump((char*)p_que_data->payload,
                                 p_que_data->payload_len,
                                 "phone msg");
                    handle_ret = app_mpack_msg_handle((char*)p_que_data->payload,
                                                      p_que_data->payload_len);
                    break;
                }
                case EMT_SYSTEM_EVENT: {
                    handle_ret = app_system_msg_handle(p_que_data);
                    break;
                }
                case EMT_SYSTEM_EMERG_MSG: {
                    handle_ret = app_emerg_msg_handle((char*)p_que_data->payload,
                                                      p_que_data->payload_len);
                    break;
                }
                case EMT_SYSTEM_EVENT_WITH_PAYLOAD: {
                    handle_ret = app_system_msg_handle_payload(p_que_data);
                    break;
                }
                default:
                    floatair_err("----, msg type not support %d",
                                 p_que_data->Header.msg_type);
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
    }
    /* The wrapped ELF message is heap-allocated by system_manager and
     * passed through the MQ as a raw pointer, so the consumer must
     * release it after handling. */
    if (p_que_data != NULL) {
        free(p_que_data);
    }
    OSAL_DELETE_MQ_MSG(msg);

}

/**
 * @brief 非阻塞消费当前 Q-8 中的全部消息。
 * @return 无返回值。
 */
static void app_msg_drain_pending(void) {
    while (1) {
        int pending = floatair_get_app_msg_queue_pending();
        int batch_count = pending;

        if (pending == 0) {
            break;
        }
        if (batch_count < 0) {
            batch_count = 1;
        }
        for (int i = 0; i < batch_count; ++i) {
#if defined(BUILD_NATIVE)
            OSAL_MQ_MSG* msg = OSAL_TIMEOUT_WAITING_MQ_MSG(
                MQ_JYT_ELFAPP_DATA_IN, CPU_SPEED_REQ_FULL, 0);
#else
            OSAL_MQ_MSG* msg = OSAL_WAITING_MQ_MSG(
                MQ_JYT_ELFAPP_DATA_IN, CPU_SPEED_REQ_FULL);
#endif

            if (msg == NULL) {
                return;
            }
            app_msg_handle(msg);
        }
    }
}

static void app_msg_recv(void) {
    uint32_t refresh_start_tick = lv_tick_get();

    while (1) {
        uint32_t refresh_elapsed = lv_tick_elaps(refresh_start_tick);

        if (refresh_elapsed >= LV_DEF_REFR_PERIOD) {
            refresh_start_tick = lv_tick_get();
            app_msg_drain_pending();
            if (!floatair_lcd_is_off()) {
                system_ui_apply_pending_screen_refresh();
                lv_timer_handler();
                /* LVGL 9 刷新后会暂停显示定时器，因此仍需显式提交 timer
                 * 产生的脏区；动画已由 lv_timer_handler 推进，直接执行显示
                 * 定时器，避免 lv_refr_now 再次推进动画并重复失效/布局。
                 * PC 仍由 SDL 主线程负责刷新，应用线程不消费其脏区。 */
#if !defined(BUILD_NATIVE)
                lv_timer_t *refresh_timer = lv_display_get_refr_timer(lv_display_get_default());
                if (refresh_timer != NULL) {
                    lv_display_refr_timer(refresh_timer);
                }
#endif
            }
            continue;
        }

        {
            uint32_t wait_ms = LV_DEF_REFR_PERIOD - refresh_elapsed;
            OSAL_MQ_MSG* msg = OSAL_TIMEOUT_WAITING_MQ_MSG(
                MQ_JYT_ELFAPP_DATA_IN, CPU_SPEED_REQ_FULL, (int)wait_ms);

            if (msg != NULL) {
                app_msg_handle(msg);
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
