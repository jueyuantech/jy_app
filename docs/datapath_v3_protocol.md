# Datapath V3 Protocol

Chinese version: [datapath_v3_protocol_cn.md](datapath_v3_protocol_cn.md)

Main code entry points:

- `common/message.h`
- `common/message.c`
- `system/system_msg_dispatch.c`
- `system/system_msg_*.c`
- `system/system_notification.c`
- `system/popups/assistant/assistant_msg.c`
- `system/stt_common.c`
- `apps/transcribe/transcribe_msg.c`
- `apps/translate/translate_msg.c`
- `apps/ai/ai_msg.c`
- `apps/prompter/prompter_msg.c`
- `apps/gallery/gallery_msg.c`

## 1. Protocol Boundary

`app_mpack_msg_handle()` receives a complete MsgPack application-layer message. Link-layer packet splitting, checksum, retransmission, and queue extraction are outside the scope of this document.

The application layer uses the following structure:

```msgpack
map(2) {
  "id": uint32,
  "payload": map(5) {
    "seq": uint32,
    "type": uint8,
    "cmd": string,
    "biz": string | nil,
    "data": map | nil
  }
}
```

Field description:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | `uint32` | Yes | Target message ID. It selects the app or system module. |
| `payload.seq` | `uint32` | No | Sequence number. ACK/NACK replies copy it back. |
| `payload.type` | `uint8` | No | Message type. See section 2. |
| `payload.cmd` | `string` | Yes | Command name. It must not be empty. |
| `payload.biz` | `string` or `nil` | No | Secondary route used by System. Normal apps usually route by `id + cmd`. |
| `payload.data` | Command-defined | No | Request `data` must be `map` or `nil`. Missing or `nil` data is accepted. Non-map request data returns `ErrDataErr`. ACK/NACK data is defined by each command. |

Length limits:

| Constant | Value | Description |
| --- | --- | --- |
| `MSG_BIZ_MAX_LEN` | 32 | Maximum cached `biz` length. Longer values are truncated. |
| `MSG_CMD_MAX_LEN` | 32 | Maximum cached `cmd` length. Longer values are truncated. |
| `MSG_STR_MAX_LEN` | 1024 | Common string read buffer length. |

## 2. Message Types

| Constant | Value | Direction | Description |
| --- | --- | --- | --- |
| `MSG_TYPE_DATA_UNRELIABLE` | `0x00` | Both | Normal data message. ACK behavior depends on the command handler. |
| `MSG_TYPE_ACK` | `0x01` | Both | Success reply. |
| `MSG_TYPE_NAK` | `0x02` | Both | Error reply. |
| `MSG_TYPE_HANDSHAKE` | `0x03` | Both | Handshake type. The common parser keeps the type value. |
| `MSG_TYPE_HEARTBEAT` | `0x04` | Both | Heartbeat type. The common parser keeps the type value. |
| `MSG_TYPE_DATA_RELIABLE` | `0x80` | Both | Reliable data message. Commands normally reply with ACK/NACK. |
| `MSG_TYPE_INVALID` | `0xFF` | Internal | Initial placeholder value. |

The common layer does not suppress ACK just because the request type is `DATA_UNRELIABLE`. The command handler decides whether to reply.

## 3. ACK / NACK

`app_mpack_create_writer()` copies `id/seq/cmd/biz` from the original message and replaces only `payload.type` and `payload.data`.

Success ACK:

```msgpack
map(2) {
  "id": uint32,
  "payload": map(5) {
    "seq": uint32,
    "type": uint8(0x01),
    "cmd": string,
    "biz": string | nil,
    "data": map(0) {}
  }
}
```

Error NACK:

```msgpack
map(2) {
  "id": uint32,
  "payload": map(5) {
    "seq": uint32,
    "type": uint8(0x02),
    "cmd": string,
    "biz": string | nil,
    "data": map(2) {
      "code": uint32,
      "msg": string
    }
  }
}
```

Error codes:

| code | Constant | Description |
| --- | --- | --- |
| 0 | `Dp_ErrNone` | Success |
| 1 | `ErrBizErr` | Business error |
| 2 | `ErrCmdErr` | Invalid command or unknown `biz/cmd` |
| 3 | `ErrIDErr` | Unregistered `id` |
| 4 | `ErrNameErr` | Name error |
| 5 | `ErrPayloadErr` | Payload error |
| 6 | `ErrSeqErr` | Sequence error |
| 7 | `ErrTypeErr` | Type error |
| 8 | `ErrDataErr` | Data error |
| 9 | `ErrBadParam` | Bad parameter |
| 10 | `ErrDataTypeMismatch` | Data type mismatch |
| 11 | `ErrNotReady` | Current state does not allow handling |
| 12 | `ErrCmdNotImplemented` | Command not implemented |
| 13 | `ErrFontNotExistFailed` | Font does not exist |
| 14 | `ErrFileNotExistFailed` | File does not exist |
| 15 | `ErrBadFilePath` | Bad file path |
| 16 | `ErrBtErr` | Bluetooth error |
| 17 | `ErrBadCRC` | CRC error |

`ErrBadCRC` has no dedicated text. When this code is returned, `msg` is `"Unknown Error"`.

## 4. Message IDs

| id | Constant | Module |
| --- | --- | --- |
| 0 | `APP_MSG_ID_SYSTEM` | System |
| 2 | `APP_MSG_ID_TRANSCRIBE` | Transcribe |
| 3 | `APP_MSG_ID_TRANSLATE` | Translate |
| 5 | `APP_MSG_ID_PROMPTER` | Prompter |
| 7 | `APP_MSG_ID_GALLERY` | Gallery |
| 8 | `APP_MSG_ID_AI` | AI |

Common processing flow:

1. Parse root `id`.
2. Parse `payload.seq/type/cmd/biz`.
3. If the current top-level app consumes host messages, dispatch to that app first.
4. If the Bluetooth disconnect mask or popup state blocks handling, return `ErrNotReady`.
5. Validate request `data`: missing or `nil` is allowed; non-map returns `ErrDataErr`.
6. Look up the registered `app_message_t` by `id`.
7. Call the target module route function.
8. Refresh the sleep timer after successful handling. Notification messages can suppress this refresh.

## 5. System Protocol

System uses `id=0` and routes by `payload.biz`.

| biz | File | Description |
| --- | --- | --- |
| `DeviceInfo` | `system_msg_devinfo.c` | Device information queries |
| `SystemConfig` | `system_msg_sysconfig.c` | System configuration queries and settings |
| `SystemStatus` | `system_msg_sysstatus.c` | Runtime status queries and limited state control |
| `SystemControl` | `system_msg_syscontrol.c` | System control, view switching, assistant, and touch injection |
| `SystemInd` | `system_msg_sysind.c` | Remote heartbeat, keep-alive, and keyword responses |
| `Notification` | `system_notification.c` | Notification add, update, and remove |
| `File` | `system_msg_file.c` | File list, write, remove, existence check, and clear-folder operations |

Unknown `biz` or unknown `cmd` returns `ErrCmdErr`.

### 5.1 DeviceInfo

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `getAll` | `{}` | `{ "manufacturer": string, "model": string, "edition": string, "sn": string, "ssn": string, "btMac": string, "btName": string, "bleMac": string, "bleName": string, "fwVer": string, "bthVer": string, "protocolVer": string }` |
| `getManufacturer` | `{}` | `{ "manufacturer": string }` |
| `getModel` | `{}` | `{ "model": string }` |
| `getEdition` | `{}` | `{ "edition": string }` |
| `getSn` | `{}` | `{ "sn": string }` |
| `getBtMac` | `{}` | `{ "btMac": string }` |
| `getBtName` | `{}` | `{ "btName": string }` |
| `getBleMac` | `{}` | `{ "bleMac": string }` |
| `getBleName` | `{}` | `{ "bleName": string }` |
| `getFwVer` | `{}` | `{ "fwVer": string }` |
| `getBthVer` | `{}` | `{ "bthVer": string }` |
| `getProtocolVer` | `{}` | `{ "protocolVer": string }` |

### 5.2 SystemConfig

| cmd | Request `data` | ACK / NACK `data` |
| --- | --- | --- |
| `getAll` | `{}` | `{ "time": uint64, "timeConfig": { "time": string, "timestamp": uint64, "timezone": string, "userFormat": string }, "displayConfig": { "mode": uint8 }, "brightness": uint8, "autoBrightnessEnabled": uint8, "fontSize": uint8, "language": string, "inactivityTimeout": uint16, "poweroffTimeout": uint16, "wearDetectionEnabled": uint8, "headGestureConfig": { "upEnabled": uint8, "downEnabled": uint8, "upDeg": int32, "downDeg": int32, "baseDeg": int32 }, "touchpadEnabled": uint8, "idleDetectionEnabled": uint8, "displayDistanceLevel": uint32, "keywordSpottingEnabled": uint8, "notificationEnabled": uint8 }` |
| `setTime` | `{ "time": uint32 }` | NACK `{ "code": 12, "msg": string }` |
| `getTimeConfig` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `setTimeConfig` | `{ "time": string, "timestamp": uint64, "timezone": string, "userFormat": string }`; `timezone` is optional | `{}` |
| `getBrightness` | `{}` | `{ "brightness": uint8 }` |
| `setBrightness` | `{ "brightness": uint8 }` | `{}` |
| `getFontSize` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `setFontSize` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `getRowSpace` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `setRowSpace` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `getLanguage` | `{}` | `{ "language": string }` |
| `setLanguage` | `{ "language": string }` | `{}` |
| `getDisplayConfig` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `setDisplayConfig` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `getDisplayDistanceLevel` | `{}` | `{ "displayDistanceLevel": uint32 }` |
| `setDisplayDistanceLevel` | `{ "displayDistanceLevel": uint32 }`, value `1..3` | `{}` |
| `getDisplayDistance` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `setDisplayDistance` | `{ "distance": uint32 }` | `{}` |
| `getDisplayPopupDepth` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `setDisplayPopupDepth` | `{ "depth": uint32 }` | `{}` |
| `getInactivityTimeout` | `{}` | `{ "inactivityTimeout": uint16 }` |
| `setInactivityTimeout` | `{ "inactivityTimeout": uint32 }` | `{}` |
| `getPoweroffTimeout` | `{}` | `{ "poweroffTimeout": uint16 }` |
| `setPoweroffTimeout` | `{ "poweroffTimeout": uint32 }` | `{}` |
| `getHeadGestureConfig` | `{}` | `{ "upEnabled": uint8, "downEnabled": uint8, "upDeg": int32, "downDeg": int32, "baseDeg": int32 }` |
| `setHeadGestureConfig` | `{ "upEnabled": uint, "downEnabled": uint, "upDeg": int32, "downDeg": int32, "baseDeg": int32 }`; angle fields are optional | `{}` |
| `getWearDetectionEnabled` | `{}` | `{ "wearDetectionEnabled": uint8 }` |
| `setWearDetectionEnabled` | `{ "wearDetectionEnabled": uint8 }` | `{}` |
| `getAutoBrightnessEnabled` | `{}` | `{ "autoBrightnessEnabled": uint8 }` |
| `setAutoBrightnessEnabled` | `{ "autoBrightnessEnabled": uint8 }` | `{}` |
| `getTouchpadEnabled` | `{}` | `{ "touchpadEnabled": uint8 }` |
| `setTouchpadEnabled` | `{ "touchpadEnabled": uint8 }` | `{}` |
| `getIdleDetectionEnabled` | `{}` | `{ "idleDetectionEnabled": uint8 }` |
| `setIdleDetectionEnabled` | `{ "idleDetectionEnabled": uint8 }` | `{}` |
| `getKeywordSpottingEnabled` | `{}` | `{ "keywordSpottingEnabled": uint8 }` |
| `setKeywordSpottingEnabled` | `{ "keywordSpottingEnabled": uint8 }` | `{}` |
| `getNotificationEnabled` | `{}` | `{ "notificationEnabled": uint8 }` |
| `setNotificationEnabled` | `{ "notificationEnabled": uint8 }` | `{}` |

`setTimeConfig.time` uses the `yyyy-MM-dd HH:mm:ss` format. Switch fields use `0/1` for disabled/enabled.

### 5.3 SystemStatus

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `getAll` | `{}` | `{ "sysState": uint8, "chargeState": uint8, "battery": uint8 }` |
| `getSysState` | `{}` | `{ "sysState": uint8 }` |
| `setSysState` | `{ "sysState": uint8 }` | `{}` |
| `getChargeState` | `{}` | `{ "chargeState": uint8 }` |
| `getBattery` | `{}` | `{ "battery": uint8 }` |
| `getRomUsage` | `{}` | `{ "total": uint32, "used": uint32, "remaining": uint32 }` |

### 5.4 SystemControl

| cmd | Request `data` | ACK / NACK `data` |
| --- | --- | --- |
| `unbind` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `factoryReset` | `{}` | `{}` |
| `reboot` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `recovery` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `getView` | `{}` | `{ "view": string }` |
| `setView` | `{ "viewName": string }` | `{}` |
| `sendTouchEvent` | `{ "event": uint8 }` | `{}` |
| `openAssistant` | `{}` | `{}` |
| `updateAssistantSttInfo` | See [6.1 Text Record](#61-text-record) | `{}` |
| `closeAssistant` | `{}` | `{}` |
| `sendHeartbeat` | `{}` | `{}` |
| `sendKeepAlive` | `{}` | `{}` |
| `sendHandshake` | `{}` | `{}` |

`sendTouchEvent.event`:

| Value | Description |
| --- | --- |
| `0x01` | Click |
| `0x02` | Double click |
| `0x03` | Long press |
| `0x04` | Swipe up |
| `0x05` | Swipe down |
| `0x06` | Swipe left |
| `0x07` | Swipe right |

Assistant text fields are defined in [6.6 Assistant Popup](#66-assistant-popup).

### 5.5 SystemInd

Receive direction:

| cmd | Received `payload.data` | Reply |
| --- | --- | --- |
| `heartBeat` | `{}` | `{}` |
| `keepAlive` | `{}` | `{}` |
| `onKeywordSpotting` | ACK `{}` or NACK `{ "code": uint32, "msg": string }` | No secondary reply |

Device report direction:

| cmd | type | data |
| --- | --- | --- |
| `onTouchEvent` | `DATA_UNRELIABLE` | `{ "event": uint8 }` |
| `onViewChangedByName` | `DATA_UNRELIABLE` | `{ "viewName": string }` |
| `onKeywordSpotting` | `DATA_RELIABLE` | `{}` |
| `onAssistantClose` | `DATA_UNRELIABLE` | `{}` |
| `onSysStateChanged` | `DATA_UNRELIABLE` | `{ "sysState": uint8 }` |
| `onChargeStateChanged` | `DATA_UNRELIABLE` | `{ "chargeState": uint8 }` |
| `onBatteryChanged` | `DATA_UNRELIABLE` | `{ "battery": uint32 }` |

### 5.6 Notification

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `addNotification` | `{ "id": uint32, "type": uint8, "title": string, "msg": string, "duration": uint32, "iconBitmap": bin, "iconBytes": bin, "level": uint8, "action": uint8 }` | `{}` |
| `updateNotification` | `{ "id": uint32, "type": uint8, "title": string, "msg": string, "duration": uint32, "iconBitmap": bin, "iconBytes": bin, "level": uint8, "action": uint8 }` | `{}` |
| `removeNotification` | `{ "id": uint32 }` | `{}` |

`addNotification` and `updateNotification` use the same `data` fields:

```msgpack
map(8) {
  "id": uint32(1001),
  "type": uint8(1),
  "title": str("New message"),
  "msg": str("You have a notification"),
  "duration": uint32(3),
  "iconBitmap": bin(1024),
  "level": uint8(1),
  "action": uint8(1)
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | `uint32` | Yes | Notification ID |
| `type` | `uint8` | No | Notification type. `2` is an incoming call; other values are handled as normal messages. |
| `title` | `string` | No | Notification title |
| `msg` | `string` | No | Notification body |
| `duration` | `uint32` or non-negative `int32` | No | Auto-close time in seconds. Default duration is used when missing. Call notifications do not auto-close. |
| `iconBitmap` | `bin` | No | Raw 32x32 L8 icon data. Length is 1024 bytes. |
| `iconBytes` | `bin` | No | Compatibility field for `iconBitmap`. Used when `iconBitmap` is missing. |
| `level` | `uint8` | No | Notification level |
| `action` | `uint8` | No | Notification action type |

At least one displayable value must be provided among `title`, `msg`, and icon data. If no icon is provided, the default icon is used.

`removeNotification`:

```msgpack
map(1) {
  "id": uint32(1001)
}
```

### 5.7 File

File types:

| Value | Constant | Description |
| --- | --- | --- |
| `0` | `SYSTEM_FILE_TYPE_DIR` | Directory |
| `1` | `SYSTEM_FILE_TYPE_FILE` | File |
| `2` | `SYSTEM_FILE_TYPE_OTHER` | Other type. Not a valid write/remove target type. |

| cmd | Request `data` | ACK / NACK `data` |
| --- | --- | --- |
| `getFileList` | `{ "dir": string }`; if `dir` is missing, the filesystem root is used | Success returns an array: `[{ "dir": string, "name": string, "type": uint8, "size": uint32, "crc32": uint32 }]`; empty directories return NACK `{ "code": 14, "msg": string }` |
| `writeFile` | Directory write: `{ "type": 0, "dir": string }`; file write uses the write-file request structure in this section | `{}` |
| `writeFileByBinary` | Same as `writeFile` | `{}` |
| `removeFile` | Remove directory: `{ "type": 0, "dir": string }`; remove file: `{ "type": 1, "dir": string, "name": string }` | `{}` |
| `isFileExist` | Check directory: `{ "type": 0, "dir": string }`; check file: `{ "type": 1, "dir": string, "name": string, "size": uint32, "crc32": uint32 }` | `{}` |
| `clearFolder` | `{ "type": 0, "dir": string }` | `{}` |

`writeFile` / `writeFileByBinary` file write request:

```msgpack
map(6) {
  "type": uint32(1),
  "dir": str("S:/prompter"),
  "name": str("demo.txt"),
  "size": uint32(4096),
  "crc32": uint32(12345),
  "pkt": map(4) {
    "total": uint32(4),
    "cur": uint32(1),
    "crc32": uint32(23456),
    "bytes": bin(...)
  }
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `type` | `uint32` | Yes | `1` for file, `0` for directory |
| `dir` | `string` | Yes | Directory path |
| `name` | `string` | Required for file writes | File name |
| `size` | `uint32` | Required for file writes | Complete file size |
| `crc32` | `uint32` | Required for file writes | Complete file CRC value |
| `pkt.total` | `uint32` | Yes | Total packet count. Must be greater than 0. |
| `pkt.cur` | `uint32` | Yes | Current packet index. Starts from 1 and must not exceed `total`. |
| `pkt.crc32` | `uint32` | Yes | Current packet CRC value |
| `pkt.bytes` | `bin` or `string` | Yes | Current packet content. Length must be greater than 0 and no more than 65535 bytes. |

File commands validate paths, existence, and CRC. Common errors are `ErrBadFilePath`, `ErrFileNotExistFailed`, and `ErrBadCRC`.

## 6. STT / AI Text Protocol

STT, Translate, AI text, and Assistant popup use the same text fields.

| id | biz | Module |
| --- | --- | --- |
| 2 | `transcribe` | Transcribe |
| 3 | `translate` | Translate |
| 8 | `ai` | AI |
| 0 | `SystemControl` | Assistant popup |

### 6.1 Text Record

`updateSttInfo` and `updateAssistantSttInfo` use one text record as `data`:

```msgpack
map(10) {
  "id": uint32(1),
  "area": uint8(0),
  "msgId": str("msg_123"),
  "msgType": uint8(0),
  "actionType": uint8(1),
  "isFinal": uint8(1),
  "user": str("User A"),
  "transcribe": str("Hello World"),
  "translate": str("Hello World"),
  "createdAt": uint64(1678888888)
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | `uint32` | Yes | Record ID |
| `area` | `uint8` | Yes | Text area. In Assistant, `0` is answer, `1` is question, and `2` is guide prompt. |
| `msgId` | `string` | No | Unique message ID. Used to update or remove the same record. |
| `msgType` | `uint8` | Yes | Text message type. It is written into the text buffer. |
| `actionType` | `uint8` | Yes | Text update action |
| `isFinal` | `uint8` | Yes | Whether this is the final result |
| `user` | `string` | No | Speaker or user |
| `transcribe` | `string` | No | Source text |
| `translate` | `string` | No | Translated text |
| `createdAt` | `uint64` | Yes | Unix timestamp in seconds |

`actionType`:

| Value | Behavior |
| --- | --- |
| `0` | set / replace. If a record with the same `msgId` exists, replace it; otherwise insert at the head of the buffer. |
| `1` | append / upsert. If a record with the same `msgId` exists, update it; otherwise insert at the head of the buffer. |
| `2` | remove by `msgId` |

The text buffer keeps up to 6 records. If `msgId` is empty, update or remove by `msgId` is limited.

### 6.2 Common Commands

Commands in this section return `{}` on success.

#### `clearView`

```msgpack
map(0) {}
```

Clears the text buffer and page text.

#### `setFontConfig`

Flat font fields:

```msgpack
map(3) {
  "weight": uint32(30),
  "wordSpace": uint32(4),
  "rowSpace": uint32(4)
}
```

Nested in `fontConfig`:

```msgpack
map(1) {
  "fontConfig": map(3) {
    "weight": uint32(30),
    "wordSpace": uint32(4),
    "rowSpace": uint32(4)
  }
}
```

| Field | Type | Description |
| --- | --- | --- |
| `weight` | `uint32` | Font size configuration value |
| `wordSpace` | `uint32` | Character spacing |
| `rowSpace` | `uint32` | Line spacing |

#### `setTextMode`

```msgpack
map(1) {
  "textMode": uint8(1)
}
```

| Value | Description |
| --- | --- |
| `0` | Default mode |
| `1` | History mode |
| `2` | Meeting mode |

#### `setAudioTrackState`

Flat audio field:

```msgpack
map(1) {
  "audioTrack": uint8(1)
}
```

Nested in `data`:

```msgpack
map(1) {
  "data": map(1) {
    "audioTrack": uint8(1)
  }
}
```

#### `setTransMode`

```msgpack
map(1) {
  "transMode": uint8(1)
}
```

| Value | Description |
| --- | --- |
| `0` | Show translated text only |
| `1` | Show source text and translated text |
| `2` | Show source text only |

#### `setMaxLine`

```msgpack
map(1) {
  "maxLine": uint32(3)
}
```

#### `setAudioSourceIndicator`

```msgpack
map(1) {
  "audioSourceIndicator": uint8(0)
}
```

| Value | Description |
| --- | --- |
| `0` | Glasses audio source |
| `1` | Phone audio source |

#### `setMicDirectional`

```msgpack
map(1) {
  "micDirectional": uint8(1)
}
```

| Value | Description |
| --- | --- |
| `0` | Omnidirectional |
| `1` | Directional |

#### `setLanguageHint`

Flat language fields:

```msgpack
map(3) {
  "mode": uint8(1),
  "source": str("en-US"),
  "target": str("zh-CN")
}
```

Nested in `languageHint`:

```msgpack
map(1) {
  "languageHint": map(3) {
    "mode": uint8(1),
    "source": str("en-US"),
    "target": str("zh-CN")
  }
}
```

| Field | Type | Description |
| --- | --- | --- |
| `mode` | `uint8` | `0` for single-language hint, `1` for dual-language hint |
| `source` | `string` | Source language |
| `target` | `string` | Target language. Required when `mode=1`. |

#### `textDirection`

Flat direction fields:

```msgpack
map(2) {
  "source": uint8(0),
  "target": uint8(1)
}
```

Nested in `textDirection`:

```msgpack
map(1) {
  "textDirection": map(2) {
    "source": uint8(0),
    "target": uint8(1)
  }
}
```

| Value | Description |
| --- | --- |
| `0` | Left to right |
| `1` | Right to left |

`target` is optional. If missing, it follows `source`.

### 6.3 Transcribe

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setFontConfig` | See [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | See [6.1 Text Record](#61-text-record) | `{}` |
| `setTextMode` | `{ "textMode": uint8 }` | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` or `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setTransMode` | `{ "transMode": uint8 }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |
| `setLanguageHint` | See [6.2 setLanguageHint](#setlanguagehint) | `{}` |
| `textDirection` | See [6.2 textDirection](#textdirection) | `{}` |

### 6.4 Translate

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setFontConfig` | See [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | See [6.1 Text Record](#61-text-record) | `{}` |
| `setTextMode` | `{ "textMode": uint8 }` | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` or `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setTransMode` | `{ "transMode": uint8 }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |
| `setLanguageHint` | See [6.2 setLanguageHint](#setlanguagehint) | `{}` |
| `textDirection` | See [6.2 textDirection](#textdirection) | `{}` |

### 6.5 AI

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setFontConfig` | See [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | See [6.1 Text Record](#61-text-record) | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` or `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |

### 6.6 Assistant Popup

Assistant popup is routed under `SystemControl`:

| id | biz | cmd | Request `data` | Success ACK `data` |
| --- | --- | --- | --- | --- |
| 0 | `SystemControl` | `openAssistant` | `{}` | `{}` |
| 0 | `SystemControl` | `updateAssistantSttInfo` | See [6.1 Text Record](#61-text-record) | `{}` |
| 0 | `SystemControl` | `closeAssistant` | `{}` | `{}` |

`updateAssistantSttInfo` uses the same text record fields as this chapter. In Assistant, `area=1` is question, `area=0` is answer, and `area=2` is guide prompt.

## 7. Prompter

Prompter uses `id=5` and routes by `cmd`.

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `getTextLayout` | `{}` | `{ "breakAll": bool, "letterSpacePx": uint, "lineSpacePx": uint, "paddingHorizontalPx": uint, "paddingVerticalPx": uint, "textSizePx": uint, "totalHeightPx": uint, "totalWidthPx": uint }` |
| `setFontConfig` | See [6.2 setFontConfig](#setfontconfig) | `{}` |
| `setFileListMenu` | See `setFileListMenu` fields in this section | `{}` |
| `setPrompterFile` | `{ "dir": string, "name": string, "size": uint32, "crc32": uint32 }` | `{}` |
| `seekTo` | `{ "offset": uint32, "length": uint32, "topMaskHeight": uint32, "bottomMaskHeight": uint32 }` | `{}` |
| `setTick` | `{ "tick": uint32 }` | `{}` |
| `setState` | `{ "state": uint32 }`, `0` pause, `1` running | `{}` |

`setFileListMenu`:

```msgpack
map(4) {
  "menuId": uint32(1),
  "itemCount": uint32(2),
  "defaultItemId": uint32(100),
  "items": array(2) [
    { "id": uint32(100), "label": str("demo.txt") },
    { "id": uint32(101), "label": str("meeting.txt") }
  ]
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `menuId` | `uint32` | Yes | Menu ID |
| `itemCount` | `uint32` | Yes | Number of menu items |
| `defaultItemId` | `uint32` | No | Default selected item ID |
| `items` | `array` | Required when `itemCount > 0` | Menu item array. Length must equal `itemCount`. |
| `items[].id` | `uint32` | Yes | Menu item ID |
| `items[].label` | `string` | Yes | Menu item display text |

Prompter reports:

| cmd | type | data |
| --- | --- | --- |
| `onMenuSelected` | `DATA_RELIABLE` | `{ "menuId": uint, "selectedItemId": uint }` |

## 8. Gallery

Gallery uses `id=7` and routes by `cmd`.

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setGalleryFile` | `{ "dir": string, "name": string }` | `{}` |

## 9. Maintenance Rules

When adding or changing a protocol, check the following:

1. Whether `id/type/error` in `common/message.h` needs an update.
2. Whether the target module has registered `app_message_t`.
3. Whether the target command is present in the corresponding `app_cmd_func_t` table.
4. Whether request `payload.data` remains `map` or `nil`.
5. Whether both success and failure paths return clear ACK/NACK.
6. Whether reports use `system_report_next_sequence()` or the module-specific sequence generator.
7. Whether this document has been updated.
