/**
 * @file system_avrcp.c
 * @brief AVRCP 跨核事件解析与原始媒体字段缓存实现。
 */
#include "system/system_avrcp.h"

#include <stddef.h>
#include <string.h>

#include "app_lcd.h"
#include "system/system.h"

static system_avrcp_snapshot_t s_snapshot = {0};
static system_avrcp_listener_t s_listener = NULL;

/**
 * @brief 将无结束符的 AVRCP 文本复制为本地 C 字符串。
 * @param[out] dst 目标字符串缓存。
 * @param[in] src 源文本。
 * @param[in] src_len 源文本字节数。
 * @return 无返回值。
 */
static void system_avrcp_copy_text(char dst[JYT_AVRCP_EVENT_TEXT_MAX_LEN + 1u],
                                   const uint8_t* src,
                                   uint16_t src_len) {
    size_t copy_len = src_len;

    if (copy_len > JYT_AVRCP_EVENT_TEXT_MAX_LEN) {
        copy_len = JYT_AVRCP_EVENT_TEXT_MAX_LEN;
    }
    if (copy_len > 0u) {
        memcpy(dst, src, copy_len);
    }
    dst[copy_len] = '\0';
}

/**
 * @brief 在亮屏状态下向页面同步最新 AVRCP 快照。
 * @return 无返回值。
 */
static void system_avrcp_notify_listener(void) {
    if (s_listener == NULL || floatair_lcd_is_off()) {
        return;
    }
    s_listener(&s_snapshot);
}

/**
 * @brief 保存播放器曲目标识，文字内容由 Title、Artist 属性消息更新。
 * @param[in] packet AVRCP 事件包。
 * @return 无返回值。
 */
static void system_avrcp_handle_track_changed(const jyt_avrcp_event_packet_t* packet) {
    s_snapshot.track_uid_msb = packet->track_uid_msb;
    s_snapshot.track_uid_lsb = packet->track_uid_lsb;
    s_snapshot.track_valid = true;
}

/**
 * @brief 处理 Title 或 Artist 属性。
 * @param[in] packet AVRCP 事件包。
 * @return 无返回值。
 */
static void system_avrcp_handle_attribute(const jyt_avrcp_event_packet_t* packet) {
    if (packet->attr_id == JYT_AVRCP_MEDIA_ATTR_TITLE) {
        system_avrcp_copy_text(s_snapshot.title,
                               packet->value,
                               packet->value_len);
    } else if (packet->attr_id == JYT_AVRCP_MEDIA_ATTR_ARTIST) {
        system_avrcp_copy_text(s_snapshot.artist,
                               packet->value,
                               packet->value_len);
    }
}

/**
 * @brief 判断播放状态值是否属于 AVRCP 标准定义。
 * @param[in] status 待校验状态值。
 * @return `true` 表示状态有效，`false` 表示无效。
 */
static bool system_avrcp_play_status_is_valid(uint8_t status) {
    return status <= JYT_AVRCP_PLAY_STATUS_REV_SEEK ||
           status == JYT_AVRCP_PLAY_STATUS_ERROR;
}

/**
 * @brief 保存 AVRCP 播放进度，并过滤未知或越界值。
 * @param[in] packet AVRCP 播放进度事件包。
 * @return 无返回值。
 */
static void system_avrcp_handle_play_progress(const jyt_avrcp_event_packet_t* packet) {
    jyt_avrcp_play_progress_t progress = {0};

    memcpy(&progress, packet->value, sizeof(progress));
    if (progress.duration_ms == 0u ||
        progress.duration_ms == UINT32_MAX ||
        progress.position_ms == UINT32_MAX) {
        s_snapshot.position_ms = 0u;
        s_snapshot.duration_ms = 0u;
        s_snapshot.progress_valid = false;
        return;
    }

    s_snapshot.duration_ms = progress.duration_ms;
    s_snapshot.position_ms = progress.position_ms > progress.duration_ms
                                 ? progress.duration_ms
                                 : progress.position_ms;
    s_snapshot.progress_source =
        (jyt_avrcp_progress_source_t)packet->attr_id;
    s_snapshot.progress_valid = true;
}

bool system_avrcp_handle_event(const JYT_ELF_MQ_MSG* msg) {
    const jyt_avrcp_event_packet_t* packet = NULL;
    size_t packet_header_size = sizeof(jyt_avrcp_event_packet_t);

    if (msg == NULL) {
        return false;
    }
    if (msg->payload_len < packet_header_size) {
        return false;
    }

    packet = (const jyt_avrcp_event_packet_t*)msg->payload;
    if (packet->magic != JYT_AVRCP_EVENT_MAGIC ||
        packet->version != JYT_AVRCP_EVENT_VERSION ||
        packet->value_len > JYT_AVRCP_EVENT_TEXT_MAX_LEN ||
        packet_header_size + packet->value_len != msg->payload_len) {
        return false;
    }

    if (packet->type == JYT_AVRCP_EVENT_TRACK_CHANGED) {
        if (packet->value_len != 0u) {
            return false;
        }
        system_avrcp_handle_track_changed(packet);
    } else if (packet->type == JYT_AVRCP_EVENT_MEDIA_ATTRIBUTE) {
        system_avrcp_handle_attribute(packet);
    } else if (packet->type == JYT_AVRCP_EVENT_PLAY_STATUS) {
        if (packet->value_len != 0u ||
            !system_avrcp_play_status_is_valid(packet->attr_id)) {
            return false;
        }
        s_snapshot.play_status = (jyt_avrcp_play_status_t)packet->attr_id;
        s_snapshot.play_status_valid = true;
    } else if (packet->type == JYT_AVRCP_EVENT_PLAY_PROGRESS) {
        if (packet->value_len != sizeof(jyt_avrcp_play_progress_t) ||
            packet->attr_id < JYT_AVRCP_PROGRESS_SOURCE_NOTIFICATION ||
            packet->attr_id > JYT_AVRCP_PROGRESS_SOURCE_METADATA) {
            return false;
        }
        system_avrcp_handle_play_progress(packet);
    } else if (packet->type == JYT_AVRCP_EVENT_DISCONNECTED) {
        if (packet->value_len != 0u) {
            return false;
        }
        memset(&s_snapshot, 0, sizeof(s_snapshot));
    } else {
        return false;
    }

    system_avrcp_notify_listener();
    return true;
}

const system_avrcp_snapshot_t* system_avrcp_get_snapshot(void) {
    return &s_snapshot;
}

void system_avrcp_set_listener(system_avrcp_listener_t listener) {
    s_listener = listener;
    system_avrcp_notify_listener();
}

void system_avrcp_clear_listener(system_avrcp_listener_t listener) {
    if (s_listener == listener) {
        s_listener = NULL;
    }
}

void system_avrcp_flush_pending_after_screen_on(void) {
    system_avrcp_notify_listener();
}

bool system_avrcp_send_control(jyt_avrcp_control_t control) {
    dev_ctl_cmd_t command = {0};

    if (control < JYT_AVRCP_CONTROL_PLAY ||
        control > JYT_AVRCP_CONTROL_VOLUME_DOWN) {
        return false;
    }

    command.dev_type = DEV_AVRCP_CTRL;
    command.control_code = (uint8_t)control;
    return system_request_device_control(&command);
}
