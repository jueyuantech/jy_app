#include <time.h>
#include "stt_common.h"

#include "cJSON.h"
#include "floatair_dbg.h"
#include "app_def.h"
#include "system/system_config_json.h"
#include "sys_adapter.h"
#include <inttypes.h>

stt_config_t stt_config = {0};
static uint32_t s_stt_font_size = 24;
stt_info_t *stt_info_buf = NULL;
lv_style_t stt_stylecur;
lv_style_t stt_stylehis;
lv_style_t stt_stylebolder;

#define STT_FLOW_Q8_PENDING_LOW 8   ///< Q-8 轻度堆积阈值，开始隔帧跳过普通 STT 更新。
#define STT_FLOW_Q8_PENDING_MID 16  ///< Q-8 中度堆积阈值，提高普通 STT 更新跳过比例。
#define STT_FLOW_Q8_PENDING_HIGH 24 ///< Q-8 高度堆积阈值，跳过全部普通 STT 更新帧。
#define STT_FLOW_MSG_TYPE_STT 0     ///< 普通 STT 正文消息类型。
#define STT_FLOW_ACTION_NEW 0       ///< STT 文本新建动作类型，流式中间结果也可能反复使用。
#define STT_FLOW_ACTION_UPDATE 1    ///< STT 文本更新动作类型。
#define STT_FLOW_AI_AREA_QUESTION 1 ///< AI 提问区域，拥塞时仍保留用于切换问答上下文。
#define STT_STALE_INTERIM_TIMEOUT_SEC 3 ///< 普通 STT 中间结果允许滞后的最大秒数。

static int s_stt_flow_queue_pending = -1;      ///< 最近一次 Q-8 剩余待处理消息数量。
static uint32_t s_stt_flow_drop_seq = 0;       ///< STT 流控跳帧计数。
static bool s_stt_update_info_skipped = false; ///< 最近一次 STT 更新是否跳过页面操作。

/**
 * @brief 判断一条 STT 记录是否为空。
 */
#define STT_INFO_IS_EMPTY(entry) \
    ((entry) == NULL || (((entry)->msgId == NULL) && ((entry)->user == NULL) && \
     ((entry)->transcribe == NULL) && ((entry)->translate == NULL)))

static uint32_t stt_resolve_font_size(uint32_t configured_size) {
    uint32_t runtime_size = configured_size;
    uint32_t resolved_size = 0;

    if (!app_fontsize_valid((int32_t)runtime_size)) {
        floatair_warn("invalid font size %" PRIu32 ", clamp to 24", configured_size);
        runtime_size = 24;
    }

    resolved_size = get_font_size_near(runtime_size);
    if (resolved_size != configured_size) {
        floatair_info("stt font size resolved from %" PRIu32 " to %" PRIu32,
                      configured_size,
                      resolved_size);
    }

    return resolved_size;
}

static bool stt_set_font_size(uint32_t configured_size) {
    const lv_font_t* font = NULL;
    uint32_t resolved_size = 0;

    resolved_size = stt_resolve_font_size(configured_size);
    font = get_font_by_size(resolved_size);
    if (font == NULL) {
        font = get_font_by_size_near(resolved_size);
    }
    if (font == NULL) {
        floatair_err("global font unavailable size=%" PRIu32, resolved_size);
        return false;
    }

    s_stt_font_size = resolved_size;
    floatair_info("stt use global font (%p) size=%" PRIu32, font, s_stt_font_size);
    return true;
}

static void stt_free_entry(stt_info_t* e) {
    if (!e) return;
    if (e->msgId) { free(e->msgId); e->msgId = NULL; }
    if (e->user) { free(e->user); e->user = NULL; }
    if (e->transcribe) { free(e->transcribe); e->transcribe = NULL; }
    if (e->translate) { free(e->translate); e->translate = NULL; }
    memset(e, 0, sizeof(stt_info_t));
}

/**
 * @brief 移动一条 STT 记录的字符串所有权。
 * @param[out] dst 目标记录，原有字符串会先释放。
 * @param[in,out] src 源记录，移动后会被清空。
 * @return 无返回值。
 */
static void stt_move_entry(stt_info_t* dst, stt_info_t* src) {
    if (!dst || !src || dst == src) return;
    stt_free_entry(dst);
    *dst = *src;
    memset(src, 0, sizeof(stt_info_t));
}

int stt_size(void) {
    if (stt_info_buf == NULL) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < STT_INFO_MAX_MSG_NUM; i++) {
        if (!STT_INFO_IS_EMPTY(&stt_info_buf[i])) n++;
    }
    return n;
}

void stt_set_flow_queue_pending(int pending) {
    s_stt_flow_queue_pending = pending;
}

bool stt_update_sttinfo_was_skipped(void) {
    return s_stt_update_info_skipped;
}

/**
 * @brief 判断 STT 记录是否为可跳过的普通中间结果。
 * @param[in] info 已解析的 STT 基础字段。
 * @return `true` 表示可按性能策略跳过页面刷新，`false` 表示必须保留。
 */
static bool stt_is_skippable_interim_update(const stt_info_t* info) {
    bool skippable_action = false;

    if (info == NULL) {
        return false;
    }

    skippable_action = (info->actionType == STT_FLOW_ACTION_NEW ||
                        info->actionType == STT_FLOW_ACTION_UPDATE);
    return info->msgType == STT_FLOW_MSG_TYPE_STT &&
           skippable_action &&
           info->isFinal == 0;
}

/**
 * @brief 根据 createdAt 判断普通 STT 中间结果是否已过期。
 * @param[in] info 已解析的 STT 基础字段。
 * @return `true` 表示消息已经明显落后，可跳过本次页面刷新，`false` 表示继续处理。
 */
static bool stt_should_skip_stale_interim_update(const stt_info_t* info) {
    time_t now = 0;
    uint64_t now_sec = 0;

    if (s_stt_flow_queue_pending <= 0) {
        return false;
    }
    if (!stt_is_skippable_interim_update(info)) {
        return false;
    }

    now = time(NULL);
    if (now <= 0) {
        return false;
    }

    now_sec = (uint64_t)now;
    if (info->createdAt >= now_sec) {
        return false;
    }

    return (now_sec - info->createdAt) > STT_STALE_INTERIM_TIMEOUT_SEC;
}

/**
 * @brief 根据 Q-8 堆积状态判断普通 STT 更新是否需要跳过页面操作。
 * @param[in] info 已解析的 STT 基础字段。
 * @param[in] msg 已解析的消息头。
 * @return `true` 表示可跳过本次页面操作，`false` 表示页面需要正常处理。
 */
static bool stt_should_skip_update_by_flow(const stt_info_t* info, const msg_pack_t* msg) {
    if (info == NULL || msg == NULL) {
        return false;
    }
    if (s_stt_flow_queue_pending < STT_FLOW_Q8_PENDING_LOW) {
        return false;
    }
    if (msg->type != MSG_TYPE_DATA_UNRELIABLE) {
        return false;
    }
    if (!stt_is_skippable_interim_update(info)) {
        return false;
    }
    if (msg->id == APP_MSG_ID_AI && info->area == STT_FLOW_AI_AREA_QUESTION) {
        return false;
    }

    s_stt_flow_drop_seq++;
    if (s_stt_flow_queue_pending >= STT_FLOW_Q8_PENDING_HIGH) {
        return true;
    }
    if (s_stt_flow_queue_pending >= STT_FLOW_Q8_PENDING_MID) {
        return (s_stt_flow_drop_seq % 3u) != 0u;
    }
    return (s_stt_flow_drop_seq % 2u) != 0u;
}

static int stt_find_by_msgid(const char* msgId) {
    if (!msgId) return -1;
    if (stt_info_buf == NULL) return -1;
    for (int i = 0; i < STT_INFO_MAX_MSG_NUM; i++) {
        if (!STT_INFO_IS_EMPTY(&stt_info_buf[i]) && stt_info_buf[i].msgId && strcmp(stt_info_buf[i].msgId, msgId) == 0) {
            return i;
        }
    }
    return -1;
}

static void stt_shift_right(int count) {
    if (stt_info_buf == NULL) {
        return;
    }
    if (count <= 0) return;
    if (count > STT_INFO_MAX_MSG_NUM) count = STT_INFO_MAX_MSG_NUM;
    if (!STT_INFO_IS_EMPTY(&stt_info_buf[STT_INFO_MAX_MSG_NUM - 1])) {
        stt_free_entry(&stt_info_buf[STT_INFO_MAX_MSG_NUM - 1]);
    }
    for (int i = STT_INFO_MAX_MSG_NUM - 1; i > 0; i--) {
        if (!STT_INFO_IS_EMPTY(&stt_info_buf[i - 1])) {
            stt_move_entry(&stt_info_buf[i], &stt_info_buf[i - 1]);
        } else {
            stt_free_entry(&stt_info_buf[i]);
        }
    }
    stt_free_entry(&stt_info_buf[0]);
}

static void stt_remove_index(int idx) {
    if (stt_info_buf == NULL) {
        return;
    }
    if (idx < 0 || idx >= STT_INFO_MAX_MSG_NUM) return;
    stt_free_entry(&stt_info_buf[idx]);
    for (int i = idx; i < STT_INFO_MAX_MSG_NUM - 1; i++) {
        if (!STT_INFO_IS_EMPTY(&stt_info_buf[i + 1])) {
            stt_move_entry(&stt_info_buf[i], &stt_info_buf[i + 1]);
        } else {
            stt_free_entry(&stt_info_buf[i]);
        }
    }
    stt_free_entry(&stt_info_buf[STT_INFO_MAX_MSG_NUM - 1]);
}

void stt_buffer_deinit(void) {
    if (stt_info_buf == NULL) {
        return;
    }
    for (int i = 0; i < STT_INFO_MAX_MSG_NUM; i++) {
        stt_free_entry(&stt_info_buf[i]);
    }
    free(stt_info_buf);
    stt_info_buf = NULL;
}

void stt_buffer_init(void) {
    if (stt_info_buf) {
        stt_buffer_deinit();
        stt_info_buf = NULL;
    }
    stt_info_buf = malloc(STT_INFO_MAX_MSG_NUM * sizeof(stt_info_t));
    floatair_assert(stt_info_buf != NULL, "stt_info_buf is NULL");
    memset(stt_info_buf, 0, STT_INFO_MAX_MSG_NUM * sizeof(stt_info_t));
}

void stt_buffer_dump(void) {
    if (stt_info_buf == NULL) {
        floatair_err("stt_info_buf is NULL");
        return;
    }
    floatair_dbg("##############stt_buffer_dump begin###########################");
    for (int i = 0; i < STT_INFO_MAX_MSG_NUM; i++) {
        floatair_dbg("          index(%d)", i);
        if (stt_info_buf[i].msgId) {
            floatair_dbg("msgId %s", stt_info_buf[i].msgId);
            floatair_dbg("msgIdlen %zu", strlen(stt_info_buf[i].msgId));
        }
        if (stt_info_buf[i].user) {
            floatair_dbg("user %s", stt_info_buf[i].user);
            floatair_dbg("userlen %zu", strlen(stt_info_buf[i].user));
        }
        if (stt_info_buf[i].transcribe) {
            floatair_dbg("transcribe %s", stt_info_buf[i].transcribe);
            floatair_dbg("transcribelen %zu", strlen(stt_info_buf[i].transcribe));
        }
        if (stt_info_buf[i].translate) {
            floatair_dbg("translate %s", stt_info_buf[i].translate);
            floatair_dbg("translatelen %zu", strlen(stt_info_buf[i].translate));
        }
        floatair_dbg("id %u", (unsigned int)stt_info_buf[i].id);
        floatair_dbg("area %d", stt_info_buf[i].area);
        floatair_dbg("isFinal %d", stt_info_buf[i].isFinal);
        floatair_dbg("msgType %d", stt_info_buf[i].msgType);
        floatair_dbg("actionType %d", stt_info_buf[i].actionType);
        floatair_dbg("createdAt %llu", (unsigned long long)stt_info_buf[i].createdAt);
    }
    floatair_dbg("##############stt_buffer_dump end  ###########################");
    return;
}

bool stt_buffer_push(stt_info_t* info) {
    if (!info) {
        floatair_err("input err");
        return false;
    }
    if (stt_info_buf == NULL) {
        stt_buffer_init();
    }
    int cur_size = stt_size();
    for (int i = 0; i < cur_size; ) {
        if (!STT_INFO_IS_EMPTY(&stt_info_buf[i]) && (stt_info_buf[i].msgType == 2 || stt_info_buf[i].msgType == 1)) {
            stt_remove_index(i);
            cur_size--;
            continue;
        }
        i++;
    }
    bool msgid_support = (info->msgId != NULL && info->msgIdLen > 0);
    if (info->actionType == 0) {
        if (!msgid_support) {
            stt_move_entry(&stt_info_buf[0], info);
            return true;
        } else {
            int idx = stt_find_by_msgid(info->msgId);
            if (idx >= 0) {
                stt_move_entry(&stt_info_buf[idx], info);
                return true;
            } else {
                stt_shift_right(STT_INFO_MAX_MSG_NUM);
                stt_move_entry(&stt_info_buf[0], info);
                return true;
            }
        }
    } else if (info->actionType == 1) {
        bool updated = false;
        if (msgid_support) {
            int idx = stt_find_by_msgid(info->msgId);
            if (idx >= 0) {
                stt_move_entry(&stt_info_buf[idx], info);
                updated = true;
            }
        }
        if (!updated) {
            stt_shift_right(STT_INFO_MAX_MSG_NUM);
            stt_move_entry(&stt_info_buf[0], info);
        }
        return true;
    } else if (info->actionType == 2) {
        if (msgid_support) {
            int idx = stt_find_by_msgid(info->msgId);
            if (idx >= 0) {
                stt_remove_index(idx);
            }
        }
        stt_free_entry(info);
        return true;
    } else {
        floatair_err("Invalid actionType: %d", info->actionType);
        return false;
    }
}

const char* stt_buffer_get_translate_by_index(size_t index) {
    if (stt_info_buf == NULL) {
        floatair_err("stt_info_buf is NULL");
        return NULL;
    }
    if (index >= STT_INFO_MAX_MSG_NUM) {
        floatair_err("index %d out of range", (int) index);
        return NULL;
    }
    if (stt_info_buf[index].translate && stt_info_buf[index].translate[0] != '\0') {
        return stt_info_buf[index].translate;
    }
    return NULL;
}

const char* stt_buffer_get_transcribe_by_index(size_t index) {
    if (stt_info_buf == NULL) {
        floatair_err("stt_info_buf is NULL");
        return NULL;
    }
    if (index >= STT_INFO_MAX_MSG_NUM) {
        floatair_err("index %d out of range", (int) index);
        return NULL;
    }
    if (stt_info_buf[index].transcribe && stt_info_buf[index].transcribe[0] != '\0') {
        return stt_info_buf[index].transcribe;
    }
    return NULL;
}

uint32_t stt_buffer_get_area_by_index(size_t index) {
    if (stt_info_buf == NULL) {
        floatair_err("stt_info_buf is NULL");
        return 0;
    }
    if (index >= STT_INFO_MAX_MSG_NUM) {
        floatair_err("index %d out of range", (int) index);
        return 0;
    }
    return stt_info_buf[index].area;
}

bool stt_buffer_get_is_final_by_index(size_t index) {
    if (stt_info_buf == NULL) {
        floatair_err("stt_info_buf is NULL");
        return false;
    }
    if (index >= STT_INFO_MAX_MSG_NUM) {
        floatair_err("index %d out of range", (int) index);
        return false;
    }
    return stt_info_buf[index].isFinal;
}

uint32_t stt_buffer_get_msg_type_by_index(size_t index) {
    if (stt_info_buf == NULL) {
        floatair_err("stt_info_buf is NULL");
        return 0;
    }
    if (index >= STT_INFO_MAX_MSG_NUM) {
        floatair_err("index %d out of range", (int) index);
        return 0;
    }
    return stt_info_buf[index].msgType;
}

uint32_t stt_buffer_get_act_type_by_index(size_t index) {
    if (stt_info_buf == NULL) {
        floatair_err("stt_info_buf is NULL");
        return 0;
    }
    if (index >= STT_INFO_MAX_MSG_NUM) {
        floatair_err("index %d out of range", (int) index);
        return 0;
    }
    return stt_info_buf[index].actionType;
}

bool stt_set_fontconfig(mpack_node_t node, msg_pack_t* msg, char* config_file) {
    app_font_info_t font_info = {0};
    uint32_t configured_size = 0;
    uint32_t resolved_size = 0;

    if (!msg || !config_file) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    mpack_node_t parse_node = node;
    mpack_node_t font_node = mpack_node_map_cstr_optional(node, "fontConfig");
    if (!mpack_node_is_missing(font_node) && !mpack_node_is_nil(font_node) &&
        mpack_node_type(font_node) == mpack_type_map) {
        parse_node = font_node;
    }

    if (!app_msg_get_u32(parse_node, false, "weight", &(font_info.weight))) {
        floatair_err("weight err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(parse_node, false, "wordSpace", &(font_info.wordSpace))) {
        floatair_err("wordSpace err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(parse_node, false, "rowSpace", &(font_info.rowSpace))) {
        floatair_err("rowSpace err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    int32_t font_size = (int32_t)font_info.weight;
    int32_t word_space = (int32_t)font_info.wordSpace;
    int32_t row_space = (int32_t)font_info.rowSpace;
    if (!app_fontsize_valid(font_size) ||
        !app_font_wordspace_valid(word_space) ||
        !app_font_rowspace_valid(row_space)) {
        floatair_warn("invalid font cfg: size=%d word=%d row=%d",
                      (int)font_size, (int)word_space, (int)row_space);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    configured_size = font_info.weight;
    resolved_size = stt_resolve_font_size(configured_size);
    if (get_font_by_size(resolved_size) == NULL && get_font_by_size_near(resolved_size) == NULL) {
        floatair_err("global stt font unavailable size=%" PRIu32, resolved_size);
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (!system_config_set_font(config_file, &font_info)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }

    if (!stt_set_font_size(font_info.weight)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    stt_style_init();
    floatair_info("font cfg ok: size=%" PRIu32 "->%" PRIu32 " word=%d row=%d",
                  configured_size,
                  resolved_size,
                  (int)word_space,
                  (int)row_space);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

bool stt_update_sttinfo(mpack_node_t node, msg_pack_t* msg) {
    stt_info_t stt_info = {0};
    bool skip_page_by_flow = false;
    bool skip_page_by_stale = false;
    bool has_created_at = false;
    uint32_t update_id = 0;
    uint8_t update_area = 0;
    uint64_t update_created_at = 0;
    s_stt_update_info_skipped = false;

    if (!msg) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(node, true, "id", &(stt_info.id))) {
        floatair_err("id err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u8(node, true, "area", &(stt_info.area))) {
        floatair_err("area err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u8(node, false, "msgType", &(stt_info.msgType))) {
        floatair_err("msgType err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u8(node, false, "actionType", &(stt_info.actionType))) {
        floatair_err("actionType err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u8(node, true, "isFinal", &(stt_info.isFinal))) {
        floatair_err("isFinal err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    {
        mpack_node_t created_at_node = mpack_node_map_cstr_optional(node, "createdAt");
        has_created_at = !mpack_node_is_missing(created_at_node);
    }
    if (!app_msg_get_u64(node, true, "createdAt", &(stt_info.createdAt))) {
        floatair_err("createdAt err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    {
        mpack_node_t parse_node = mpack_node_map_cstr_optional(node, "msgId");
        if (!mpack_node_is_missing(parse_node) && !mpack_node_is_nil(parse_node)) {
            size_t size = 0;

            if (mpack_node_type(parse_node) != mpack_type_str) {
                floatair_err("type err key: msgId");
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            size = mpack_node_strlen(parse_node);
            if (size > 0) {
                stt_info.msgId = malloc(size + 1);
                floatair_assert(stt_info.msgId != NULL, "malloc msgId failed");
                memcpy(stt_info.msgId, mpack_node_str(parse_node), size);
                stt_info.msgId[size] = '\0';
                stt_info.msgIdLen = (uint32_t)size;
            }
        }
    }
    {
        mpack_node_t parse_node = mpack_node_map_cstr_optional(node, "user");
        if (!mpack_node_is_missing(parse_node) && !mpack_node_is_nil(parse_node)) {
            size_t size = 0;

            if (mpack_node_type(parse_node) != mpack_type_str) {
                floatair_err("type err key: user");
                stt_free_entry(&stt_info);
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            size = mpack_node_strlen(parse_node);
            if (size > 0) {
                stt_info.user = malloc(size + 1);
                floatair_assert(stt_info.user != NULL, "malloc user failed");
                memcpy(stt_info.user, mpack_node_str(parse_node), size);
                stt_info.user[size] = '\0';
                stt_info.userLen = (uint32_t)size;
            }
        }
    }
    {
        mpack_node_t parse_node = mpack_node_map_cstr_optional(node, "transcribe");
        if (!mpack_node_is_missing(parse_node) && !mpack_node_is_nil(parse_node)) {
            size_t size = 0;

            if (mpack_node_type(parse_node) != mpack_type_str) {
                floatair_err("type err key: transcribe");
                stt_free_entry(&stt_info);
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            size = mpack_node_strlen(parse_node);
            if (size > 0) {
                stt_info.transcribe = malloc(size + 1);
                floatair_assert(stt_info.transcribe != NULL, "malloc transcribe failed");
                memcpy(stt_info.transcribe, mpack_node_str(parse_node), size);
                stt_info.transcribe[size] = '\0';
                stt_info.transcribeLen = (uint32_t)size;
            }
        }
    }
    {
        mpack_node_t parse_node = mpack_node_map_cstr_optional(node, "translate");
        if (!mpack_node_is_missing(parse_node) && !mpack_node_is_nil(parse_node)) {
            size_t size = 0;

            if (mpack_node_type(parse_node) != mpack_type_str) {
                floatair_err("type err key: translate");
                stt_free_entry(&stt_info);
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            size = mpack_node_strlen(parse_node);
            if (size > 0) {
                stt_info.translate = malloc(size + 1);
                floatair_assert(stt_info.translate != NULL, "malloc translate failed");
                memcpy(stt_info.translate, mpack_node_str(parse_node), size);
                stt_info.translate[size] = '\0';
                stt_info.translateLen = (uint32_t)size;
            }
        }
    }

    skip_page_by_flow = stt_should_skip_update_by_flow(&stt_info, msg);
    skip_page_by_stale = has_created_at &&
                         (msg->type == MSG_TYPE_DATA_UNRELIABLE) &&
                         stt_should_skip_stale_interim_update(&stt_info);
    update_id = stt_info.id;
    update_area = stt_info.area;
    update_created_at = stt_info.createdAt;
    if (!stt_buffer_push(&stt_info)) {
        stt_free_entry(&stt_info);
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (skip_page_by_stale) {
        s_stt_update_info_skipped = true;
        floatair_info("skip stale stt page update pending=%d id=%" PRIu32 " area=%u createdAt=%" PRIu64,
                      s_stt_flow_queue_pending,
                      update_id,
                      (unsigned int)update_area,
                      update_created_at);
        stt_free_entry(&stt_info);
        return false;
    }
    if (skip_page_by_flow) {
        s_stt_update_info_skipped = true;
        floatair_info("skip stt page update by Q-8 pending=%d id=%" PRIu32 " area=%u",
                      s_stt_flow_queue_pending,
                      update_id,
                      (unsigned int)update_area);
        stt_free_entry(&stt_info);
        return false;
    }
    stt_free_entry(&stt_info);
    if (msg->type == MSG_TYPE_DATA_RELIABLE) {
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }
    return true;
}

bool stt_set_textmode(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u8(node, false, "textMode", &(stt_config.textMode))) {
        floatair_err("textMode err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (stt_config.textMode != TEXTMODE_DEFAULT && stt_config.textMode != TEXTMODE_HISTORY && stt_config.textMode != TEXTMODE_MEETING) {
        floatair_err("textMode err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

bool stt_set_audiotrackstate(mpack_node_t node, msg_pack_t* msg) {
    uint8_t audio_track = 0;

    if (!msg) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    mpack_node_t at = mpack_node_map_cstr_optional(node, "audioTrack");
    if (mpack_node_is_missing(at)) {
        mpack_node_t data = mpack_node_map_cstr_optional(node, "data");
        if (!mpack_node_is_missing(data) && !mpack_node_is_nil(data) &&
            mpack_node_type(data) == mpack_type_map) {
            at = mpack_node_map_cstr_optional(data, "audioTrack");
        }
    }
    if (mpack_node_is_missing(at) || mpack_node_is_nil(at) || mpack_node_type(at) != mpack_type_uint) {
        floatair_err("audioTrack err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    audio_track = mpack_node_u8(at);
    floatair_info("app msg u8: (audioTrack) %u ", audio_track);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

bool stt_set_transmode(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u8(node, false, "transMode", &(stt_config.transMode))) {
        floatair_err("transMode err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (stt_config.transMode != TRANSMODE_SHOW_ONLY_TRANS && stt_config.transMode != TRANSMODE_SHOW_DUAL && stt_config.transMode != TRANSMODE_SHOW_ONLY_ORI) {
        floatair_err("transMode err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

bool stt_set_maxline(mpack_node_t node, msg_pack_t* msg) {
    uint32_t max_line = 0;

    if (!msg) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(node, false, "maxLine", &max_line)) {
        floatair_err("maxLine err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

bool stt_set_audiosourceindicator(mpack_node_t node,
                                  msg_pack_t* msg) {
    if (!msg) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u8(node, false, "audioSourceIndicator", &(stt_config.audioSourceIndicator))) {
        floatair_err("audioSourceIndicator err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (stt_config.audioSourceIndicator != AUDIOSOURCE_GLASSES && stt_config.audioSourceIndicator != AUDIOSOURCE_PHONE) {
        floatair_err("audioSourceIndicator err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

bool stt_set_micdirectional(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u8(node, false, "micDirectional", &(stt_config.micDirectional))) {
        floatair_err("micDirectional err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (stt_config.micDirectional != OMNIDIRECTIONAL && stt_config.micDirectional != DIRECTIONAL) {
        floatair_err("micDirectional err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

bool stt_set_languagehint(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    mpack_node_t parse_node = node;
    mpack_node_t language_hint_node = mpack_node_map_cstr_optional(node, "languageHint");
    if (!mpack_node_is_missing(language_hint_node) && !mpack_node_is_nil(language_hint_node) &&
        mpack_node_type(language_hint_node) == mpack_type_map) {
        parse_node = language_hint_node;
    }

    if (!app_msg_get_u8(parse_node, false, "mode", &(stt_config.language_hint))) {
        floatair_err("language_hint err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (stt_config.language_hint > 1) {
        floatair_err("language_hint err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    mpack_node_t src_node = mpack_node_map_cstr_optional(parse_node, "source");
    if (mpack_node_is_missing(src_node) || mpack_node_is_nil(src_node) || mpack_node_type(src_node) != mpack_type_str) {
        floatair_err("source err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    size_t src_len = mpack_node_strlen(src_node);
    if (src_len == 0) {
        floatair_err("source err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    size_t src_copy = src_len;
    if (src_copy >= STT_CONFIG_MAX_LANGUAGE_LEN) {
        src_copy = STT_CONFIG_MAX_LANGUAGE_LEN - 1;
    }
    memcpy(stt_config.language_source, mpack_node_str(src_node), src_copy);
    stt_config.language_source[src_copy] = '\0';

    if (stt_config.language_hint == 0) {
        stt_config.language_target[0] = '\0';
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    mpack_node_t dst_node = mpack_node_map_cstr_optional(parse_node, "target");
    if (mpack_node_is_missing(dst_node) || mpack_node_is_nil(dst_node) || mpack_node_type(dst_node) != mpack_type_str) {
        floatair_err("target err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    size_t dst_len = mpack_node_strlen(dst_node);
    if (dst_len == 0) {
        floatair_err("target err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    size_t dst_copy = dst_len;
    if (dst_copy >= STT_CONFIG_MAX_LANGUAGE_LEN) {
        dst_copy = STT_CONFIG_MAX_LANGUAGE_LEN - 1;
    }
    memcpy(stt_config.language_target, mpack_node_str(dst_node), dst_copy);
    stt_config.language_target[dst_copy] = '\0';
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

bool stt_set_textdirection(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) {
        floatair_err("input err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    mpack_node_t parse_node = node;
    mpack_node_t text_dir_node = mpack_node_map_cstr_optional(node, "textDirection");
    if (!mpack_node_is_missing(text_dir_node) && !mpack_node_is_nil(text_dir_node) &&
        mpack_node_type(text_dir_node) == mpack_type_map) {
        parse_node = text_dir_node;
    }

    uint8_t src_dir = TEXT_DIRECTION_LTR;
    if (!app_msg_get_u8(parse_node, false, "source", &src_dir)) {
        floatair_err("textDirection err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (src_dir != TEXT_DIRECTION_LTR && src_dir != TEXT_DIRECTION_RTL) {
        floatair_err("textDirection err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    uint8_t dst_dir = src_dir;
    if (!app_msg_get_u8(parse_node, true, "target", &dst_dir)) {
        floatair_err("textDirection err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (dst_dir != TEXT_DIRECTION_LTR && dst_dir != TEXT_DIRECTION_RTL) {
        floatair_err("textDirection err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    stt_config.sourceTextDirection = src_dir;
    stt_config.targetTextDirection = dst_dir;
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

void stt_service_init(char *config_file) {
    app_font_info_t font_info = {0};

    if (!config_file) {
        floatair_err("input err");
        return;
    }
    floatair_info("config file: %s", config_file);

    if (!system_config_get_font(config_file, &font_info)) {
        floatair_err("font config missing");
        return;
    }

    if (!stt_set_font_size(font_info.weight)) {
        return;
    }
    memset(&stt_config, 0, sizeof(stt_config));
    stt_buffer_init();
}

void stt_service_deinit(void) {
    stt_style_deinit();
    s_stt_font_size = 24;
    stt_buffer_deinit();
}

/**
 * @brief 初始化 STT 服务状态快照。
 * @param[out] snapshot 快照对象。
 * @return 无返回值。
 */
void stt_service_snapshot_init(stt_service_snapshot_t* snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
}

/**
 * @brief 挂起当前 STT 服务全局状态。
 * @param[out] snapshot 用于保存当前状态的快照对象。
 * @return `true` 表示挂起成功，`false` 表示参数无效或快照已被占用。
 */
bool stt_service_suspend(stt_service_snapshot_t* snapshot) {
    if (snapshot == NULL) {
        return false;
    }
    if (snapshot->buffer != NULL) {
        floatair_warn("stt service snapshot already active");
        return false;
    }

    snapshot->config = stt_config;
    snapshot->font_size = s_stt_font_size;
    snapshot->buffer = stt_info_buf;

    stt_info_buf = NULL;
    memset(&stt_config, 0, sizeof(stt_config));
    s_stt_font_size = 24;
    return true;
}

/**
 * @brief 恢复此前挂起的 STT 服务全局状态，并释放当前临时 STT 服务资源。
 * @param[in,out] snapshot 待恢复的快照对象。
 * @return 无返回值。
 */
void stt_service_resume(stt_service_snapshot_t* snapshot) {
    if (snapshot == NULL) {
        return;
    }

    stt_service_deinit();
    if (snapshot->buffer != NULL) {
        stt_config = snapshot->config;
        s_stt_font_size = snapshot->font_size;
        stt_info_buf = snapshot->buffer;
        stt_style_init();
    }

    stt_service_snapshot_init(snapshot);
}

const lv_font_t* stt_get_font(void) {
    const lv_font_t* font = get_font_by_size(s_stt_font_size);

    if (font == NULL) {
        font = get_font_by_size_near(s_stt_font_size);
    }
    if (font == NULL) {
        floatair_err("font is NULL, size=%" PRIu32, s_stt_font_size);
        return NULL;
    }

    return font;
}

uint32_t stt_get_font_size(void) {
    return s_stt_font_size;
}

uint32_t stt_get_font_height(void) {
    return get_font_height(stt_get_font());
}

static bool inited_cur = false;
static bool inited_his = false;
static bool inited_bdr = false;

void stt_style_init(void) {
    if (inited_cur) {
        lv_style_reset(&stt_stylecur);
    } else {
        lv_style_init(&stt_stylecur);
        inited_cur = true;
    }
    lv_style_set_opa(&stt_stylecur, LV_OPA_100);

    if (inited_his) {
        lv_style_reset(&stt_stylehis);
    } else {
        lv_style_init(&stt_stylehis);
        inited_his = true;
    }
    lv_style_set_opa(&stt_stylehis, LV_OPA_70);

    if (inited_bdr) {
        lv_style_reset(&stt_stylebolder);
    } else {
        lv_style_init(&stt_stylebolder);
        inited_bdr = true;
    }
    lv_style_set_radius(&stt_stylebolder, 10);
    lv_style_set_opa(&stt_stylebolder, LV_OPA_100);
    lv_style_set_border_color(&stt_stylebolder, lv_color_white());
    lv_style_set_border_width(&stt_stylebolder, 2);
    lv_style_set_border_opa(&stt_stylebolder, LV_OPA_100);
    lv_style_set_border_side(&stt_stylebolder, LV_BORDER_SIDE_FULL);
    lv_style_set_pad_hor(&stt_stylebolder, get_system_font_word_space());
    lv_style_set_pad_ver(&stt_stylebolder, get_system_font_row_space());
    floatair_info("font rowSpace: %" PRIu32 ", wordSpace: %" PRIu32,
                  get_system_font_row_space(),
                  get_system_font_word_space());
}

void stt_style_deinit(void) {
    if (inited_cur) {
        lv_style_reset(&stt_stylecur);
        inited_cur = false;
    }
    if (inited_his) {
        lv_style_reset(&stt_stylehis);
        inited_his = false;
    }
    if (inited_bdr) {
        lv_style_reset(&stt_stylebolder);
        inited_bdr = false;
    }
}
