/**
 * @brief 模拟器消息队列与本地消息的 OSAL 适配。
 */
#pragma once
#include "floatair_def.h"
#include "floatair_dbg.h"
#include "elf_common.h"
#include "sim_socket.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int Waiting4SystemMessage(void *pMsg);
extern int Waiting4SystemMessageTimeout(void *pMsg, int timeout_ms);
extern int simulator_shutdown_requested(void);
extern void simulator_handle_local_mq_msg(MQ_NAME_ID qid,
                                          LOCAL_MSG_ID msg_id,
                                          const void *payload,
                                          uint32_t payload_len);

extern long int GetMicroSecondsCount(void);

static inline OSAL_MQ_MSG *__linux_osal_waiting_mq_msg(MQ_NAME_ID qid, CpuSpeedReq speed)
{
    (void)qid;
    (void)speed;
    JYT_ELF_MQ_MSG *sysmsg = NULL;
    int r = Waiting4SystemMessage(&sysmsg);
    if (r != (int)sizeof(JYT_ELF_MQ_MSG*) || !sysmsg) {
        if (simulator_shutdown_requested()) {
            pthread_exit(NULL);
        }
        return NULL;
    }
    OSAL_MQ_MSG *m = (OSAL_MQ_MSG *)malloc(sizeof(OSAL_MQ_MSG));
    if (!m) { free(sysmsg); return NULL; }
    m->header.chip = 0;
    m->header.core = 0;
    m->header.dest = (uint16_t)qid;
    m->header.id = LMID_ELFMSG_WRAP;
    m->header.pdu_size = (uint16_t)sizeof(void*);
    m->pdu.ptr[0] = sysmsg;
    return m;
}

/**
 * @brief 限时等待系统消息，关闭时退出消息线程。
 * @param qid 目标消息队列标识。
 * @param speed CPU 速度请求，模拟器不使用。
 * @param timeout 等待毫秒数，负数表示无限等待。
 * @return 成功返回消息，超时或失败返回 NULL；关闭时不返回。
 */
static inline OSAL_MQ_MSG *__linux_osal_timeout_waiting_mq_msg(MQ_NAME_ID qid, CpuSpeedReq speed, int timeout)
{
    (void)qid;
    (void)speed;
    JYT_ELF_MQ_MSG *sysmsg = NULL;
    int r = Waiting4SystemMessageTimeout(&sysmsg, timeout);
    if (r != (int)sizeof(JYT_ELF_MQ_MSG*) || !sysmsg) {
        /* 关闭请求必须终止消息线程，普通超时仍返回空消息。 */
        if (r < 0 && simulator_shutdown_requested()) {
            pthread_exit(NULL);
        }
        return NULL;
    }
    OSAL_MQ_MSG *m = (OSAL_MQ_MSG *)malloc(sizeof(OSAL_MQ_MSG));
    if (!m) { free(sysmsg); return NULL; }
    m->header.chip = 0;
    m->header.core = 0;
    m->header.dest = (uint16_t)qid;
    m->header.id = LMID_ELFMSG_WRAP;
    m->header.pdu_size = (uint16_t)sizeof(void*);
    m->pdu.ptr[0] = sysmsg;
    return m;
}

static inline void __linux_osal_delete_mq_msg(OSAL_MQ_MSG *msg)
{
    if (!msg) return;
    if (msg->header.id == LMID_ELFMSG_WRAP) {
        JYT_ELF_MQ_MSG *p = (JYT_ELF_MQ_MSG *)msg->pdu.ptr[0];
        (void)p;
        // if (p) free(p);
    }
    free(msg);
}

#undef OSAL_TIMEOUT_WAITING_MQ_MSG
#undef OSAL_WAITING_MQ_MSG
#undef OSAL_DELETE_MQ_MSG
#define OSAL_TIMEOUT_WAITING_MQ_MSG(qid, spd, timeout) __linux_osal_timeout_waiting_mq_msg(qid, spd, timeout)
#define OSAL_WAITING_MQ_MSG(qid, spd) __linux_osal_waiting_mq_msg(qid, spd)
#define OSAL_DELETE_MQ_MSG(msg) __linux_osal_delete_mq_msg(msg)

#undef OSAL_INST_SEND_LOCAL_MQ_MSG
#define OSAL_INST_SEND_LOCAL_MQ_MSG(qid, MSG_ID, PDU, PDU_SIZE) \
    do {                                                       \
        simulator_handle_local_mq_msg((qid),                   \
                                       (MSG_ID),                \
                                       (PDU),                   \
                                       (PDU_SIZE));             \
    } while (0)

#undef OSAL_INST_SEND_REMOTE_MQ_MSG
#define OSAL_INST_SEND_REMOTE_MQ_MSG(qid, DEST_CHIP, DEST_CORE, DEST_QUEUE, MSG_ID, PDU, PDU_SIZE) \
    do {                                                                                           \
        (void)(qid);                                                                                \
        (void)(DEST_CHIP);                                                                          \
        (void)(DEST_CORE);                                                                          \
        (void)(DEST_QUEUE);                                                                         \
        (void)(MSG_ID);                                                                             \
        (void)(PDU);                                                                                \
        (void)(PDU_SIZE);                                                                           \
    } while (0)

#ifdef __cplusplus
}
#endif
