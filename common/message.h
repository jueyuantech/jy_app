/**
 * @file message.h
 * @brief Application message framework interfaces and types
 */
#pragma once

#include <time.h>
#include "elf_common.h"

#include <limits.h>
#include <mpack.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define APP_MSG_ID_SYSTEM (0)

// Private base for application message IDs; IDs start from this value. 1-1000 are reserved.
#define APP_MSG_ID_PRIVATE_BASE (0)
#define APP_MSG_ID_HOME         (APP_MSG_ID_PRIVATE_BASE + 1)
#define APP_MSG_ID_TRANSCRIBE   (APP_MSG_ID_PRIVATE_BASE + 2)
#define APP_MSG_ID_TRANSLATE    (APP_MSG_ID_PRIVATE_BASE + 3)
#define APP_MSG_ID_NAVIGATION   (APP_MSG_ID_PRIVATE_BASE + 4)
#define APP_MSG_ID_PROMPTER     (APP_MSG_ID_PRIVATE_BASE + 5)
#define APP_MSG_ID_MUSIC        (APP_MSG_ID_PRIVATE_BASE + 6)
#define APP_MSG_ID_GALLERY      (APP_MSG_ID_PRIVATE_BASE + 7)
#define APP_MSG_ID_AI           (APP_MSG_ID_PRIVATE_BASE + 8)
#define APP_MSG_ID_READER       (APP_MSG_ID_PRIVATE_BASE + 9)
#define APP_MSG_ID_OTA          (APP_MSG_ID_PRIVATE_BASE + 10)
#define APP_MSG_ID_POWEROFF     (APP_MSG_ID_PRIVATE_BASE + 11)
#define APP_MSG_ID_POWERON      (APP_MSG_ID_PRIVATE_BASE + 12)
#define APP_MSG_ID_GUIDE        (APP_MSG_ID_PRIVATE_BASE + 13)
#define APP_MSG_ID_LANGSELECTION (APP_MSG_ID_PRIVATE_BASE + 14)

#define MSG_BIZ_MAX_LEN (32)
#define MSG_CMD_MAX_LEN (32)
#define MSG_STR_MAX_LEN (1024)


/**
 * @brief Message type
 */
#define MSG_TYPE_DATA_RELIABLE 0x80
#define MSG_TYPE_ACK 0x01
#define MSG_TYPE_NAK 0x02
#define MSG_TYPE_HANDSHAKE 0x03
#define MSG_TYPE_HEARTBEAT 0x04
#define MSG_TYPE_DATA_UNRELIABLE 0x00
#define MSG_TYPE_INVALID 0xFF


/**
 * @brief Message data-plane error code
 */
typedef enum {
    Dp_ErrNone            = 0,  ///< None
    ErrBizErr             = 1,  ///< Business error
    ErrCmdErr             = 2,  ///< Command error
    ErrIDErr              = 3,  ///< ID error
    ErrNameErr            = 4,  ///< Name error
    ErrPayloadErr         = 5,  ///< Payload error
    ErrSeqErr             = 6,  ///< Sequence error
    ErrTypeErr            = 7,  ///< Type error
    ErrDataErr            = 8,  ///< Data error
    ErrBadParam           = 9,  ///< Bad parameter error
    ErrDataTypeMismatch   = 10, ///< Data type mismatch error
    ErrNotReady           = 11, ///< Not ready error
    ErrCmdNotImplemented  = 12, ///< Command not implemented
    ErrFontNotExistFailed = 13, ///< Font not exist
    ErrFileNotExistFailed = 14, ///< File not exist
    ErrBadFilePath        = 15, ///< Bad file path
    ErrBtErr              = 16, ///< Bluetooth error
    ErrBadCRC             = 17, ///< Bad CRC error
} MsgDpErr;

/**
 * @brief Writer helper for constructing mpack message
 */
typedef struct {
    mpack_writer_t writer; ///< mpack writer
    char* buffer;          ///< internal buffer
    size_t size;           ///< buffer size
} msg_pack_writer_t;

/**
 * @brief Parsed message pack from mpack node
 */
typedef struct {
    uint32_t id;               ///< message id
    uint32_t sequence;         ///< sequence number
    char biz[MSG_BIZ_MAX_LEN]; ///< business domain
    char cmd[MSG_CMD_MAX_LEN]; ///< command name
    uint8_t type;         ///< request type
} msg_pack_t;

/**
 * @brief Negative acknowledgement payload
 */
typedef struct {
    uint32_t code;   ///< error code
    const char* msg; ///< error message
} msg_pack_nck_t;

typedef bool (*jyt_elf_mq_cb_func)(mpack_node_t, msg_pack_t*);
typedef bool (*jty_cmd_func)(mpack_node_t, msg_pack_t*);

/**
 * @brief Application message registration record
 */
typedef struct {
    uint32_t id;           ///< message id
    char* name;            ///< message name
    jyt_elf_mq_cb_func cb; ///< message callback
} app_message_t;

/**
 * @brief Command handler registration record
 */
typedef struct {
    char* cmd;         ///< command string
    jty_cmd_func func; ///< handler function
} app_cmd_func_t;

/**
 * @brief Initialize application message subsystem
 */
void app_msg_init(void);
/**
 * @brief Deinitialize application message subsystem and free resources
 */
void app_msg_deinit(void);
/**
 * @brief Register or update a message handler
 * @param[in] msg message registration record
 * @return 0 on success; negative on error
 */
int app_msg_register(app_message_t* msg);
/**
 * @brief Delete a registered message by ID
 * @param[in] msg_id message ID
 * @return 0 on success; negative on not found or error
 */
int app_msg_delete(uint32_t msg_id);
/**
 * @brief Update an existing message registration
 * @param[in] msg message registration record
 * @return 0 on success; negative on not found or error
 */
int app_msg_update(app_message_t* msg);
/**
 * @brief Query a registered message by ID
 * @param[in] msg_id message ID
 * @return pointer to app_message_t or NULL if not found
 */
app_message_t* app_msg_query(uint32_t msg_id);
/**
 * @brief Dump a MsgPack summary for debugging
 * @param[in] msg buffer pointer
 * @param[in] msg_size buffer size
 * @param[in] tag tag string
 */
void app_msg_dump_summary(const char* msg, size_t msg_size, const char* tag);
/**
 * @brief Dump a MsgPack buffer for debugging
 * @param[in] msg buffer pointer
 * @param[in] msg_size buffer size
 * @param[in] tag tag string
 */
void app_msg_dump(char* msg, size_t msg_size, const char* tag);
/**
 * @brief Parse payload map to msg_pack_t fields
 * @param[in] node payload root node
 * @param[out] msg parsed message pack
 * @return true on success
 */
bool app_msg_parse_header(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief Handle incoming host mpack message
 * @param[in] msg buffer
 * @param[in] msg_size buffer size
 * @return true on success
 */
bool app_mpack_msg_handle(char* msg, size_t msg_size);
/**
 * @brief Handle system event message
 * @param[in] msg system MQ message
 * @return true on success
 */
bool app_system_msg_handle(JYT_ELF_MQ_MSG* msg);
/**
 * @brief Handle system emergency message
 * @param[in] msg buffer
 * @param[in] msg_size buffer size
 * @return true on success
 */
bool app_emerg_msg_handle(char* msg, size_t msg_size);
/**
 * @brief Handle system event message with payload
 * @param[in] msg system MQ message
 * @return true on success
 */
bool app_system_msg_handle_payload(JYT_ELF_MQ_MSG* msg);

/**
 * @brief Create a msg_pack_t object
 * @return allocated pointer or NULL on failure
 */
msg_pack_t* app_mpackmsg_create(void);
/**
 * @brief Destroy a msg_pack_t object
 * @param[in] msg pointer to message
 */
void app_mpackmsg_destroy(msg_pack_t* msg);
/**
 * @brief Create writer bound to a msg_pack_t
 * @param[in] msg message context
 * @param[in] type message type
 * @return writer helper or NULL on failure
 */
msg_pack_writer_t* app_mpack_create_writer(msg_pack_t* msg, uint8_t type);
/**
 * @brief Destroy writer helper and free buffer
 * @param[in] writer writer helper
 */
void app_mpack_writer_destroy(msg_pack_writer_t* writer);
/**
 * @brief Finalize writer and send to host
 * @param[in] writer writer helper
 * @return true on success
 */
bool app_mpack_send_writer(msg_pack_writer_t* writer);
/**
 * @brief Send acknowledgement
 * @param[in] msg message context
 * @param[in] err_code error code
 * @return true on success
 */
bool app_mpack_send_ack(msg_pack_t* msg, MsgDpErr err_code);

/**
 * @brief Get uint8 field from map
 * @param[in] node map node
 * @param[in] optional whether key is optional
 * @param[in] key key string
 * @param[out] data output value
 * @return true on success
 */
bool app_msg_get_u8(mpack_node_t node, bool optional, const char* key, uint8_t* data);
/**
 * @brief Get uint16 field from map
 */
bool app_msg_get_u16(mpack_node_t node, bool optional, const char* key, uint16_t* data);
/**
 * @brief Get int32 field from map
 */
bool app_msg_get_32(mpack_node_t node, bool optional, const char* key, int32_t* data);
/**
 * @brief Get uint32 field from map
 */
bool app_msg_get_u32(mpack_node_t node, bool optional, const char* key, uint32_t* data);
/**
 * @brief Get uint64 field from map
 */
bool app_msg_get_u64(mpack_node_t node, bool optional, const char* key, uint64_t* data);
/**
 * @brief Get string field from map (copied into buffer)
 * @param[in] node map node
 * @param[in] key key string
 * @param[out] data buffer
 * @param[in] size buffer size
 * @return copied length (excluding null)
 */
size_t app_msg_get_str(mpack_node_t node, const char* key, char* data, size_t size);
/**
 * @brief Get float field from map
 */
bool app_msg_get_float(mpack_node_t node, bool optional, const char* key, float* data);
/**
 * @brief Get bool field from map
 * Accepts both mpack_type_bool and numeric 0/1 for compatibility.
 */
bool app_msg_get_bool(mpack_node_t node, bool optional, const char* key, bool* data);

/**
 * @brief Translate error code to message string
 * @param[in] err_code error code
 * @return message string
 */
const char* app_msg_get_err_msg(uint32_t err_code);
