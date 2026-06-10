#include "sys_adapter.h"
#include "floatair_dbg.h"
#include "elf_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "sim_socket.h"
#include "simulator_platform.h"
#include "lvgl.h"

#include <pthread.h>
#include <errno.h>

// Global variables defined in common/floatair_run.c

// We rely on linker to find them.

extern jyt_section_data_t g_section_data;
extern bt_info g_bt_info;

static __thread int g_simulator_lvgl_lock_depth = 0;

const char *floatair_os_version_string(void) {
    return "floatair_simulator";
}

bool system_factory_reset(void) {
    return false;
}

bool system_reboot(void) {
    return false;
}

bool system_recovery(void) {
    return false;
}

char* system_get_view(void) {
    return NULL;
}

bool system_set_view(const char* view) {
    if (view == NULL) {
        floatair_err("view is NULL");
        return false;
    }
    return false;
}

 

void system_delay_ms(unsigned int ms) {
    simulator_platform_sleep_ms(ms);
}

static bool simulator_lvgl_is_ui_critical_held(void) {
    return g_simulator_lvgl_lock_depth > 0;
}

void simulator_lvgl_enter_ui_critical(void) {
    if (g_simulator_lvgl_lock_depth == 0) {
        lv_lock();
    }
    g_simulator_lvgl_lock_depth++;
}

void simulator_lvgl_leave_ui_critical(void) {
    if (g_simulator_lvgl_lock_depth <= 0) {
        g_simulator_lvgl_lock_depth = 0;
        return;
    }
    g_simulator_lvgl_lock_depth--;
    if (g_simulator_lvgl_lock_depth == 0) {
        lv_unlock();
    }
}

uint32_t simulator_lv_timer_handler(void) {
    return 1;
}

long int GetTimeUs(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long int)(ts.tv_sec * 1000000L + ts.tv_nsec / 1000L);
}

void jyt_get_bt_info(bt_info* bt_info) {
    if (!bt_info) {
        floatair_err("bt_info is NULL");
        return;
    }
    memset(bt_info, 0, sizeof(*bt_info));
    strncpy(bt_info->bt_addr, "00:11:22:33:44:55", sizeof(bt_info->bt_addr) - 1);
    strncpy(bt_info->bt_name, "JYT-BT", sizeof(bt_info->bt_name) - 1);
    strncpy(bt_info->ble_addr, "00:11:22:33:44:55", sizeof(bt_info->ble_addr) - 1);
    strncpy(bt_info->ble_name, "JYT-BT-BLE", sizeof(bt_info->ble_name) - 1);
    floatair_info("Done");
}

void jyt_get_ft_info(jyt_section_data_t* ftinfo) {
    if (!ftinfo) {
        floatair_err("ftinfo is NULL");
        return;
    }
    memset(ftinfo, 0, sizeof(jyt_section_data_t));
    memcpy(ftinfo->jyt_psn, "2026020600000001", strlen("2026020600000001"));
    memcpy(ftinfo->jyt_ssn, "2026020600000001", strlen("2026020600000001"));
    memcpy(ftinfo->jyt_manufacture, "JYT", strlen("JYT"));
    memcpy(ftinfo->jyt_model, "JYT-FT", strlen("JYT-FT"));
    memcpy(ftinfo->jyt_edition, "001", strlen("001"));
    ftinfo->jyt_default_volumn = 50;
    ftinfo->jyt_default_brightness = 500;

    floatair_info("Done");
}

int jyt_get_bat_state(void* arg) {
    union bat_state_t batt;

    if (arg == NULL) {
        floatair_err("arg is NULL");
        return -1;
    }

    batt.value = 0;
    batt.bat_chg_combo.soc = 100;
    batt.bat_chg_combo.charger_mode = 0;
    batt.bat_chg_combo.voltage_mv = 4200;
    *(uint32_t*)arg = batt.value;
    return 0;
}

/* ---------------- Linux 内部消息队列 ---------------- */
typedef struct MQNode {
    JYT_ELF_MQ_MSG* msg;
    struct MQNode* next;
} MQNode;

static pthread_mutex_t g_mq_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_mq_cond = PTHREAD_COND_INITIALIZER;
static MQNode* g_mq_head = NULL;
static MQNode* g_mq_tail = NULL;
static int g_shutdown_requested = 0;

static void mq_post(JYT_ELF_MQ_MSG* m) {
    if (!m) return;
    MQNode* node = (MQNode*)malloc(sizeof(MQNode));
    if (!node) {
        free(m);
        return;
    }
    node->msg = m;
    node->next = NULL;

    pthread_mutex_lock(&g_mq_mutex);
    if (g_mq_tail) {
        g_mq_tail->next = node;
        g_mq_tail = node;
    } else {
        g_mq_head = g_mq_tail = node;
    }
    pthread_cond_signal(&g_mq_cond);
    pthread_mutex_unlock(&g_mq_mutex);
}

/**
 * @brief 向模拟器系统消息队列投递一个系统事件。
 * @param[in] event_type 系统事件类型，定义见 `SYSTEM_EVENT_TYPE`。
 * @return 无返回值。
 */
void simulator_post_system_event_ex(uint16_t event_type,
                                    uint8_t simple_data,
                                    const void* payload,
                                    uint16_t payload_len) {
    size_t alloc_size = sizeof(JYT_ELF_MQ_MSG) + payload_len;
    JYT_ELF_MQ_MSG* msg = (JYT_ELF_MQ_MSG*)malloc(alloc_size);

    if (!msg) {
        floatair_err("alloc system event msg failed, evt=%u", (unsigned)event_type);
        return;
    }

    msg->Header.msg_type = EMT_SYSTEM_EVENT_WITH_PAYLOAD;
    msg->Header.simple_data = simple_data;
    msg->Header.event_type = event_type;
    msg->payload_len = payload_len;
    if (payload && payload_len > 0) {
        memcpy(msg->payload, payload, payload_len);
    }
    mq_post(msg);
}

void simulator_post_system_event(uint16_t event_type) {
    simulator_post_system_event_ex(event_type, 0, NULL, 0);
}

/* ---------------- 将 socket 消息转投递到 MQ 的后台线程 ---------------- */
static pthread_t g_socket_worker;
static int g_socket_worker_started = 0;
static int g_socket_worker_running = 0;

static void mq_clear(void) {
    MQNode* node = g_mq_head;
    while (node) {
        MQNode* next = node->next;
        free(node->msg);
        free(node);
        node = next;
    }
    g_mq_head = NULL;
    g_mq_tail = NULL;
}

static void* socket_worker_thread(void* param) {
    (void)param;
    if (sim_socket_rx_init(9999) != 0) {
        floatair_err("sim_socket_rx_init failed");
        return NULL;
    }
    while (g_socket_worker_running) {
        JYT_ELF_MQ_MSG* p = NULL;
        int r = sim_socket_rx_wait(&p, 100);
        if (r == (int)sizeof(JYT_ELF_MQ_MSG*)) {
            if (p && p->Header.msg_type == EMT_SYSTEM_EVENT) {
                p->Header.msg_type = EMT_SYSTEM_EVENT_WITH_PAYLOAD;
            }
            mq_post(p);
        } else if (r < 0) {
            simulator_platform_sleep_ms(10);
        }
    }
    return NULL;
}

static void sys_start_socket_worker(void) {
    pthread_mutex_lock(&g_mq_mutex);
    if (!g_socket_worker_started) {
        g_socket_worker_running = 1;
        pthread_create(&g_socket_worker, NULL, socket_worker_thread, NULL);
        g_socket_worker_started = 1;
    }
    pthread_mutex_unlock(&g_mq_mutex);
}

int Waiting4SystemMessage(void* pMsg) {
    if (!pMsg) return -1;

    if (simulator_lvgl_is_ui_critical_held()) {
        simulator_lvgl_leave_ui_critical();
    }
    sys_start_socket_worker();

    pthread_mutex_lock(&g_mq_mutex);
    while (g_mq_head == NULL && !g_shutdown_requested) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 20 * 1000 * 1000;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }

        pthread_cond_timedwait(&g_mq_cond, &g_mq_mutex, &ts);
    }

    if (g_shutdown_requested) {
        pthread_mutex_unlock(&g_mq_mutex);
        *(JYT_ELF_MQ_MSG**)pMsg = NULL;
        return -1;
    }

    MQNode* node = g_mq_head;
    g_mq_head = node->next;
    if (g_mq_head == NULL) g_mq_tail = NULL;
    pthread_mutex_unlock(&g_mq_mutex);

    simulator_lvgl_enter_ui_critical();
    *(JYT_ELF_MQ_MSG**)pMsg = node->msg;
    free(node);
    return (int)sizeof(JYT_ELF_MQ_MSG*);
}

void simulator_request_shutdown(void) {
    pthread_mutex_lock(&g_mq_mutex);
    g_shutdown_requested = 1;
    pthread_cond_broadcast(&g_mq_cond);
    pthread_mutex_unlock(&g_mq_mutex);
}

int simulator_shutdown_requested(void) {
    pthread_mutex_lock(&g_mq_mutex);
    int shutdown_requested = g_shutdown_requested;
    pthread_mutex_unlock(&g_mq_mutex);
    return shutdown_requested;
}

void simulator_shutdown_runtime(void) {
    simulator_request_shutdown();

    pthread_mutex_lock(&g_mq_mutex);
    g_socket_worker_running = 0;
    pthread_cond_broadcast(&g_mq_cond);
    pthread_mutex_unlock(&g_mq_mutex);

    sim_socket_tx_close();

    if (g_socket_worker_started) {
        pthread_join(g_socket_worker, NULL);
        g_socket_worker_started = 0;
    }

    pthread_mutex_lock(&g_mq_mutex);
    mq_clear();
    pthread_mutex_unlock(&g_mq_mutex);
}


// Missing functions implementation

uint32_t hal_crc32(void* data, uint16_t len) {
    static const uint32_t crc32_tab[256] = {
        0x00000000,0x77073096,0xee0e612c,0x990951ba,0x076dc419,0x706af48f,0xe963a535,0x9e6495a3,
        0x0edb8832,0x79dcb8a4,0xe0d5e91e,0x97d2d988,0x09b64c2b,0x7eb17cbd,0xe7b82d07,0x90bf1d91,
        0x1db71064,0x6ab020f2,0xf3b97148,0x84be41de,0x1adad47d,0x6ddde4eb,0xf4d4b551,0x83d385c7,
        0x136c9856,0x646ba8c0,0xfd62f97a,0x8a65c9ec,0x14015c4f,0x63066cd9,0xfa0f3d63,0x8d080df5,
        0x3b6e20c8,0x4c69105e,0xd56041e4,0xa2677172,0x3c03e4d1,0x4b04d447,0xd20d85fd,0xa50ab56b,
        0x35b5a8fa,0x42b2986c,0xdbbbc9d6,0xacbcf940,0x32d86ce3,0x45df5c75,0xdcd60dcf,0xabd13d59,
        0x26d930ac,0x51de003a,0xc8d75180,0xbfd06116,0x21b4f4b5,0x56b3c423,0xcfba9599,0xb8bda50f,
        0x2802b89e,0x5f058808,0xc60cd9b2,0xb10be924,0x2f6f7c87,0x58684c11,0xc1611dab,0xb6662d3d,
        0x76dc4190,0x01db7106,0x98d220bc,0xefd5102a,0x71b18589,0x06b6b51f,0x9fbfe4a5,0xe8b8d433,
        0x7807c9a2,0x0f00f934,0x9609a88e,0xe10e9818,0x7f6a0dbb,0x086d3d2d,0x91646c97,0xe6635c01,
        0x6b6b51f4,0x1c6c6162,0x856530d8,0xf262004e,0x6c0695ed,0x1b01a57b,0x8208f4c1,0xf50fc457,
        0x65b0d9c6,0x12b7e950,0x8bbeb8ea,0xfcb9887c,0x62dd1ddf,0x15da2d49,0x8cd37cf3,0xfbd44c65,
        0x4db26158,0x3ab551ce,0xa3bc0074,0xd4bb30e2,0x4adfa541,0x3dd895d7,0xa4d1c46d,0xd3d6f4fb,
        0x4369e96a,0x346ed9fc,0xad678846,0xda60b8d0,0x44042d73,0x33031de5,0xaa0a4c5f,0xdd0d7cc9,
        0x5005713c,0x270241aa,0xbe0b1010,0xc90c2086,0x5768b525,0x206f85b3,0xb966d409,0xce61e49f,
        0x5edef90e,0x29d9c998,0xb0d09822,0xc7d7a8b4,0x59b33d17,0x2eb40d81,0xb7bd5c3b,0xc0ba6cad,
        0xedb88320,0x9abfb3b6,0x03b6e20c,0x74b1d29a,0xead54739,0x9dd277af,0x04db2615,0x73dc1683,
        0xe3630b12,0x94643b84,0x0d6d6a3e,0x7a6a5aa8,0xe40ecf0b,0x9309ff9d,0x0a00ae27,0x7d079eb1,
        0xf00f9344,0x8708a3d2,0x1e01f268,0x6906c2fe,0xf762575d,0x806567cb,0x196c3671,0x6e6b06e7,
        0xfed41b76,0x89d32be0,0x10da7a5a,0x67dd4acc,0xf9b9df6f,0x8ebeeff9,0x17b7be43,0x60b08ed5,
        0xd6d6a3e8,0xa1d1937e,0x38d8c2c4,0x4fdff252,0xd1bb67f1,0xa6bc5767,0x3fb506dd,0x48b2364b,
        0xd80d2bda,0xaf0a1b4c,0x36034af6,0x41047a60,0xdf60efc3,0xa867df55,0x316e8eef,0x4669be79,
        0xcb61b38c,0xbc66831a,0x256fd2a0,0x5268e236,0xcc0c7795,0xbb0b4703,0x220216b9,0x5505262f,
        0xc5ba3bbe,0xb2bd0b28,0x2bb45a92,0x5cb36a04,0xc2d7ffa7,0xb5d0cf31,0x2cd99e8b,0x5bdeae1d,
        0x9b64c2b0,0xec63f226,0x756aa39c,0x026d930a,0x9c0906a9,0xeb0e363f,0x72076785,0x05005713,
        0x95bf4a82,0xe2b87a14,0x7bb12bae,0x0cb61b38,0x92d28e9b,0xe5d5be0d,0x7cdcefb7,0x0bdbdf21,
        0x86d3d2d4,0xf1d4e242,0x68ddb3f8,0x1fda836e,0x81be16cd,0xf6b9265b,0x6fb077e1,0x18b74777,
        0x88085ae6,0xff0f6a70,0x66063bca,0x11010b5c,0x8f659eff,0xf862ae69,0x616bffd3,0x166ccf45,
        0xa00ae278,0xd70dd2ee,0x4e048354,0x3903b3c2,0xa7672661,0xd06016f7,0x4969474d,0x3e6e77db,
        0xaed16a4a,0xd9d65adc,0x40df0b66,0x37d83bf0,0xa9bcae53,0xdebb9ec5,0x47b2cf7f,0x30b5ffe9,
        0xbdbdf21c,0xcabac28a,0x53b39330,0x24b4a3a6,0xbad03605,0xcdd70693,0x54de5729,0x23d967bf,
        0xb3667a2e,0xc4614ab8,0x5d681b02,0x2a6f2b94,0xb40bbe37,0xc30c8ea1,0x5a05df1b,0x2d02ef8d
    };
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) crc = crc32_tab[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void send2host(void* buf, uint32_t bufsize) {
    if (!buf || bufsize == 0) return;
    
    static int inited = 0;
    if (!inited) {
        if (sim_socket_tx_init_from_config(0) == 0) {
            inited = 1;
        }
    }

    uint32_t crc = hal_crc32(buf, (uint16_t)bufsize);
    size_t out_len = 8u + (size_t)bufsize;
    unsigned char* out = (unsigned char*)malloc(out_len);
    if (!out) return;
    
    out[0] = (unsigned char)((bufsize >> 24) & 0xFF);
    out[1] = (unsigned char)((bufsize >> 16) & 0xFF);
    out[2] = (unsigned char)((bufsize >> 8) & 0xFF);
    out[3] = (unsigned char)(bufsize & 0xFF);
    out[4] = (unsigned char)((crc >> 24) & 0xFF);
    out[5] = (unsigned char)((crc >> 16) & 0xFF);
    out[6] = (unsigned char)((crc >> 8) & 0xFF);
    out[7] = (unsigned char)(crc & 0xFF);
    memcpy(out + 8, buf, bufsize);
    
    floatair_info("[send2host] len=%u crc=0x%08X -> %s",
                  (unsigned)bufsize,
                  (unsigned)crc,
                  sim_socket_get_server_info());
    sim_socket_tx_send_raw(out, out_len);
    free(out);
}

void jyt_dual_screen_set_root_distance(int distance) { floatair_info("root distance=%d", distance); }
int jyt_dual_scree_root_pos_x_trans(int x_pos) { return x_pos; }
int jyt_dual_scree_root_pos_y_trans(int y_pos) { return y_pos; }
int jyt_dual_scree_node_pos_x_trans(int delta_z, int x_pos) { return x_pos; }

int set_system_time_from_string(const char* time_str, const char* format) {
    if (!time_str || !format) {
        floatair_err("time_str or format is NULL");
        return -1;
    }
    floatair_info("time_str=%s format=%s", time_str, format);
    FLOATAIR_UNUSED(time_str);
    FLOATAIR_UNUSED(format);
    return 0;
}

int jyt_nuttx_lcd_capture(uint8_t* buf, int size) {
    if (!buf || size == 0) return -1;
    floatair_info("lcd capture size=%d", size);
    return 0;
}

// Timer implementation
typedef struct {
    uint32_t interval_ms;
    uint32_t timer_id;
    volatile int running;
    int auto_destroy;
    int detached;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} linux_timer_t;

typedef struct timer_node {
    linux_timer_t* t;
    struct timer_node* next;
} timer_node_t;

static pthread_mutex_t g_timer_mtx = PTHREAD_MUTEX_INITIALIZER;
static timer_node_t* g_timers = NULL;

static void timer_registry_add(linux_timer_t* t) {
    if (!t) return;
    timer_node_t* n = (timer_node_t*)malloc(sizeof(timer_node_t));
    if (!n) return;
    n->t = t;
    pthread_mutex_lock(&g_timer_mtx);
    n->next = g_timers;
    g_timers = n;
    pthread_mutex_unlock(&g_timer_mtx);
}

static bool timer_registry_remove(linux_timer_t* t) {
    if (!t) return false;
    bool removed = false;
    pthread_mutex_lock(&g_timer_mtx);
    timer_node_t* prev = NULL;
    timer_node_t* cur = g_timers;
    while (cur) {
        if (cur->t == t) {
            if (prev) prev->next = cur->next;
            else g_timers = cur->next;
            free(cur);
            removed = true;
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&g_timer_mtx);
    return removed;
}

static bool timer_registry_contains(linux_timer_t* t) {
    if (!t) return false;
    bool found = false;
    pthread_mutex_lock(&g_timer_mtx);
    for (timer_node_t* cur = g_timers; cur; cur = cur->next) {
        if (cur->t == t) {
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_timer_mtx);
    return found;
}

static bool timer_wait_interval(linux_timer_t* t) {
    if (!t) return false;

    pthread_mutex_lock(&t->lock);
    while (t->running) {
        struct timespec deadline = {0};
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += t->interval_ms / 1000;
        deadline.tv_nsec += (long)(t->interval_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }

        int ret = pthread_cond_timedwait(&t->cond, &t->lock, &deadline);
        if (ret == ETIMEDOUT) {
            break;
        }
    }
    bool running = (t->running != 0);
    pthread_mutex_unlock(&t->lock);
    return running;
}

static void timer_request_stop(linux_timer_t* t) {
    if (!t) return;
    pthread_mutex_lock(&t->lock);
    t->running = 0;
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

static void timer_destroy(linux_timer_t* t) {
    if (!t) return;
    pthread_cond_destroy(&t->cond);
    pthread_mutex_destroy(&t->lock);
    free(t);
}

static void* jyt_timer_worker(void* p) {
    linux_timer_t* t = (linux_timer_t*)p;
    while (timer_wait_interval(t)) {
        
        size_t alloc = sizeof(JYT_ELF_MQ_MSG) + sizeof(uint32_t);
        JYT_ELF_MQ_MSG* m = (JYT_ELF_MQ_MSG*)malloc(alloc);
        if (!m) continue;
        m->Header.msg_type = EMT_SYSTEM_EVENT_WITH_PAYLOAD;
        m->Header.simple_data = 0;
        m->Header.event_type = SET_JYT_TIMER_TRIGGER;
        m->payload_len = (uint16_t)sizeof(uint32_t);
        memcpy(m->payload, &t->timer_id, sizeof(uint32_t));
        mq_post(m);
        if (t->auto_destroy) {
            break;
        }
    }
    if (t->auto_destroy) {
        (void)timer_registry_remove(t);
        timer_destroy(t);
    }
    return NULL;
}

void* jyt_timer_create_and_start(uint32_t timeout_ms, uint32_t timer_id, int auto_destroy) {
    linux_timer_t* t = (linux_timer_t*)malloc(sizeof(linux_timer_t));
    if (!t) return NULL;
    t->interval_ms = timeout_ms;
    t->timer_id = timer_id;
    t->running = 1;
    t->auto_destroy = auto_destroy ? 1 : 0;
    t->detached = 0;
    pthread_mutex_init(&t->lock, NULL);
    pthread_cond_init(&t->cond, NULL);
    
    int r = pthread_create(&t->thread, NULL, jyt_timer_worker, t);
    if (r != 0) { 
        timer_destroy(t);
        return NULL; 
    }
    timer_registry_add(t);
    if (t->auto_destroy) {
        pthread_detach(t->thread);
        t->detached = 1;
    }
    return t;
}

void jyt_timer_stop(void* timer) { 
    if (!timer) return;
    linux_timer_t* t = (linux_timer_t*)timer;
    if (!timer_registry_contains(t)) return;
    timer_request_stop(t);
    if (!t->detached) {
        pthread_join(t->thread, NULL);
        (void)timer_registry_remove(t);
        timer_destroy(t);
    }
}

void jyt_timer_restart(void* timer) { 
    if (!timer) return;
    linux_timer_t* t = (linux_timer_t*)timer;
    if (!timer_registry_contains(t)) return;
    pthread_mutex_lock(&t->lock);
    if (t->running) {
        pthread_cond_signal(&t->cond);
    }
    pthread_mutex_unlock(&t->lock);
}

void jyt_timer_delete(void* timer) {
    if (!timer) return;
    linux_timer_t* t = (linux_timer_t*)timer;
    if (!timer_registry_contains(t)) return;
    timer_request_stop(t);
    if (!t->detached) {
        pthread_join(t->thread, NULL);
        (void)timer_registry_remove(t);
        timer_destroy(t);
    }
}

static const uint16_t crc16ccitt_tab[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78,
};

uint16_t hal_crc16_ccitt_v1(void *data, uint16_t size, uint16_t init) {
    if (!data || size == 0) return init;
    uint16_t v = init;
    uint8_t *pSrc = (uint8_t *)data;
    for (size_t i = 0; i < size; i++) {
        v = (v >> 8) ^ crc16ccitt_tab[(v ^ pSrc[i]) & 0xff];
    }
    return v;
}

uint16_t hal_crc16_ccitt(void *data, uint16_t size) {
    return hal_crc16_ccitt_v1(data, size, 0);
}


long int GetMicroSecondsCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long int)(ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL);
}
