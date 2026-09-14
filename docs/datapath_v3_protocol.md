# JYTek Datapath V3 Protocol

Chinese version: [datapath_v3_protocol_cn.md](datapath_v3_protocol_cn.md)

Main code entry points:

- `common/message.h`
- `common/message.c`
- `system/system_msg_dispatch.c`
- `system/system_msg_*.c`
- `system/system_notification.c`
- `system/system_msg_draw.c`
- `system/popups/assistant/assistant_msg.c`
- `system/stt_common.c`
- `apps/common/speech/speech_msg.c`
- `apps/ai/ai_msg.c`
- `apps/prompter/prompter_msg.c`
- `apps/gallery/gallery_msg.c`
- `apps/navigation/navigation_msg.c`
- `apps/recorder/recorder_msg.c`
- `apps/imagefusion/imagefusion_msg.c`

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
| 18 | `ErrScreenOff` | Device screen is off, command rejected |
| 19 | `ErrGuideStepMismatch` | Command is not allowed in the current guide step |

## 4. Message IDs

| id | Constant | Module |
| --- | --- | --- |
| 0 | `APP_MSG_ID_SYSTEM` | System |
| 1 | `APP_MSG_ID_HOME` | Home; no independent business commands |
| 2 | `APP_MSG_ID_TRANSCRIBE` | Transcribe |
| 3 | `APP_MSG_ID_TRANSLATE` | Translate |
| 4 | `APP_MSG_ID_NAVIGATION` | Navigation |
| 5 | `APP_MSG_ID_PROMPTER` | Prompter |
| 7 | `APP_MSG_ID_GALLERY` | Gallery |
| 8 | `APP_MSG_ID_AI` | AI |
| 13 | `APP_MSG_ID_GUIDE` | Guide; controlled through `SystemControl` |
| 1001 | `APP_MSG_ID_IMAGEFUSION` | ImageFusion |
| 1002 | `APP_MSG_ID_RECORDER` | Recorder |

Common processing flow:

1. Parse root `id`.
2. Parse `payload.seq/type/cmd/biz`.
3. If the screen is off and the command is not allowed while the screen is off, return `ErrScreenOff`.
4. If the current top-level app consumes host messages, dispatch to that app first.
5. If the Bluetooth disconnect mask or popup state blocks handling, return `ErrNotReady`.
6. If the beginner guide is active and the command is unrelated to the current guide step, return `ErrGuideStepMismatch`.
7. Validate request `data`: missing or `nil` is allowed; non-map returns `ErrDataErr`.
8. Look up the registered `app_message_t` by `id`.
9. Call the target module route function.
10. Refresh the sleep timer after successful handling. Notification messages can suppress this refresh.

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
| `Toast` | `system_msg_toast.c` | Toast popup display |
| `TapMsgbox` | `system_msg_tap_msgbox.c` | Confirmation prompt and download progress |
| `File` | `system_msg_file.c` | File list, write, remove, existence check, and clear-folder operations |
| `Draw` | `system_msg_draw.c` | Draw text, images, and progress bars on the device display |

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
| `getAll` | `{}` | `{ "time": uint64, "timeConfig": { "time": string, "timestamp": uint64, "timezone": string, "userFormat": string }, "displayConfig": { "mode": uint8 }, "brightness": uint8, "autoBrightnessEnabled": uint8, "fontSize": uint8, "language": string, "homeUnits": string[], "inactivityTimeout": uint16, "poweroffTimeout": uint16, "wearDetectionEnabled": uint8, "headGestureConfig": { "upEnabled": uint8, "downEnabled": uint8, "upDeg": int32, "downDeg": int32, "baseDeg": int32 }, "imuDoubleTapEnabled": uint8, "touchpadEnabled": uint8, "idleDetectionEnabled": uint8, "displayDistanceLevel": uint32, "keywordSpottingEnabled": uint8, "notificationEnabled": uint8 }` |
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
| `getHomeUnits` | `{}` | `{ "homeUnits": string[] }` |
| `setHomeUnits` | `{ "homeUnits": string[] }` | `{}` |
| `getHomeMenuConfig` | `{}` | `{ "all": uint32[], "visible": uint32[], "selected": uint32 }` |
| `setHomeMenuConfig` | `{ "visible": uint32[], "selected": uint32 }` | `{}`; `selected` must be included in `visible` |
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
| `getImuDoubleTapEnabled` | `{}` | `{ "imuDoubleTapEnabled": uint8 }` |
| `setImuDoubleTapEnabled` | `{ "imuDoubleTapEnabled": uint8 }` | `{}` |
| `getTouchpadEnabled` | `{}` | `{ "touchpadEnabled": uint8 }` |
| `setTouchpadEnabled` | `{ "touchpadEnabled": uint8 }` | `{}` |
| `getIdleDetectionEnabled` | `{}` | `{ "idleDetectionEnabled": uint8 }` |
| `setIdleDetectionEnabled` | `{ "idleDetectionEnabled": uint8 }` | `{}` |
| `getKeywordSpottingEnabled` | `{}` | `{ "keywordSpottingEnabled": uint8 }` |
| `setKeywordSpottingEnabled` | `{ "keywordSpottingEnabled": uint8 }` | `{}` |
| `getNotificationEnabled` | `{}` | `{ "notificationEnabled": uint8 }` |
| `setNotificationEnabled` | `{ "notificationEnabled": uint8 }` | `{}` |

`setTimeConfig.time` uses the `yyyy-MM-dd HH:mm:ss` format. Switch fields use `0/1` for disabled/enabled. When `autoBrightnessEnabled` is enabled, `setBrightness` returns NACK `ErrNotReady` and does not change the LCD brightness.

The Jytek home protocol units are `prompter(5)`, `translate(3)`, `transcribe(2)`, `ai(8)`, and `navigation(4)`.

### 5.3 SystemStatus

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `getAll` | `{}` | `{ "sysState": uint8, "chargeState": uint8, "battery": uint8 }` |
| `getSysState` | `{}` | `{ "sysState": uint8 }` |
| `setSysState` | `{ "sysState": uint8 }` | `{}` |
| `getChargeState` | `{}` | `{ "chargeState": uint8 }` |
| `getBattery` | `{}` | `{ "battery": uint8 }` |
| `getAttachmentState` | `{}` | `{ "attachments": [{ "attachmentSide": uint8, "attachmentType": uint8 }] }` |
| `getRomUsage` | `{}` | `{ "total": uint32, "used": uint32, "remaining": uint32 }` |

`getAttachmentState.attachments` always contains one item for the master side and one for the slave side. It returns the most recently received attachment types cached in `jy_app` memory. See `onAttachmentTypeChanged` in [5.5 SystemInd](#55-systemind) for the `attachmentSide` and `attachmentType` values.

`getRomUsage` reports the LFSD file system containing `/jyt_d`; all three capacity fields are measured in bytes.

### 5.4 SystemControl

| cmd | Request `data` | ACK / NACK `data` |
| --- | --- | --- |
| `unbind` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `factoryReset` | `{}` | `{}` |
| `reboot` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `recovery` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `getView` | `{}` | `{ "view": string }` |
| `setView` | `{ "viewName": string }` | `{}` |
| `getScreenOnScope` | `{}` | `{ "scope": uint8 }` |
| `setScreenOnScope` | `{ "scope": uint8 }` | `{}` |
| `sendTouchEvent` | `{ "event": uint8 }` | `{}` |
| `openGuide` | `{}` | `{}` |
| `closeGuide` | `{}` | `{}` |
| `openAssistant` | `{}` | `{}` |
| `updateAssistantSttInfo` | See [6.1 Text Record](#61-text-record) | `{}` |
| `closeAssistant` | `{}` | `{}` |
| `sendHeartbeat` | `{}` | `{}` |
| `sendKeepAlive` | `{}` | `{}` |
| `sendHandshake` | `{}` | `{}` |
| `setProgressVisible` | Show: `{ "visible": 1, "text": string, "opa": 0..100 }`; hide: `{ "visible": 0 }` | `{}` |
| `setUploadProgressVisible` | `{ "visible": bool/0/1 }` | Succeeds only when the current page supports upload progress and a file transfer is active; Jytek supports it on `prompter` and `gallery` |

`factoryReset` writes and syncs a cleanup marker under `/jyt_d`, preserves that marker, and deletes all remaining user data without copying files back from ROMFS. Whether cleanup succeeds or fails, the device turns the display off before triggering an assert reset and does not guarantee an ACK; the marker is retained on failure. On the next startup, a remaining marker resumes cleanup before system configuration is loaded; another failure triggers another assert reset so a later startup can retry. The marker is removed only after all user data has been deleted. Configuration is then reconstructed from ROMFS defaults plus any new sparse LFSD overrides.

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

`getScreenOnScope` returns the current in-memory temporary screen-on scope. `setScreenOnScope.scope` changes only that in-memory state and does not change the persisted inactivity timeout configuration:

| Value | Description |
| --- | --- |
| `0` | Disable temporary screen-on mode and restart the inactivity timer from now |
| `1` | Keep the screen on within the current app; restore the inactivity timer after leaving the current app or phone disconnection |
| `2` | Keep the screen on for the current phone connection; restore the inactivity timer after phone disconnection |

Assistant text fields are defined in [6.6 Assistant Popup](#66-assistant-popup).

`openGuide` resets `SystemConfig.userguide` to `"false"` and routes to the Guide app. `closeGuide` resets guide runtime state, writes `SystemConfig.userguide` to `"true"`, returns to Home, and reports `onViewChangedByName` with `viewName="home"`. Phone-triggered `openGuide` and `closeGuide` do not report `onGuideOpen` or `onGuideClose`. Device-side guide entry reports `onGuideOpen`; completing or skipping the guide on device reports `onGuideClose` and then `onViewChangedByName` with `viewName="home"`. Guide internal transitions do not report `onViewChangedByName`.

### 5.5 SystemInd

Receive direction:

| cmd | Received `payload.data` | Reply |
| --- | --- | --- |
| `heartBeat` | `{}` | `{}` |
| `keepAlive` | `{}` | `{}` |
| `onKeywordSpotting` | ACK `{}` or NACK `{ "code": uint32, "msg": string }` | No secondary reply |
| `onGuideOpen` | `{}` | `{}` |
| `onGuideClose` | `{}` | `{}` |

Device report direction:

| cmd | type | data |
| --- | --- | --- |
| `onTouchEvent` | `DATA_UNRELIABLE` | `{ "event": uint8 }` |
| `onViewChangedByName` | `DATA_UNRELIABLE` | `{ "viewName": string }` |
| `onKeywordSpotting` | `DATA_RELIABLE` | `{}` |
| `onAssistantOpen` | `DATA_UNRELIABLE` | `{}` |
| `onAssistantClose` | `DATA_UNRELIABLE` | `{}` |
| `onGuideOpen` | `DATA_UNRELIABLE` | `{}` |
| `onGuideClose` | `DATA_UNRELIABLE` | `{}` |
| `onSysStateChanged` | `DATA_UNRELIABLE` | `{ "sysState": uint8, "trigger": string }` |
| `onChargeStateChanged` | `DATA_UNRELIABLE` | `{ "chargeState": uint8 }` |
| `onBatteryChanged` | `DATA_UNRELIABLE` | `{ "battery": uint32 }` |
| `onBrightnessChanged` | `DATA_UNRELIABLE` | `{ "brightness": uint8 }` |
| `onAttachmentTypeChanged` | `DATA_UNRELIABLE` | `{ "attachmentType": uint8, "attachmentSide": uint8 }` |

`onAttachmentTypeChanged.attachmentType`:

| Value | Description |
| --- | --- |
| `0` | No attachment |
| `1` | Glasses case |
| `2` | External power |
| `3` | Speaker |

`onAttachmentTypeChanged.attachmentSide`:

| Value | Description |
| --- | --- |
| `0` | Master side (right side on the current hardware) |
| `1` | Slave side (left side on the current hardware) |

Each event updates only the indicated side. Consumers should retain the latest type for both sides; an `attachmentType` of `0` means that side detached, not that every attachment detached.

For `onSysStateChanged.sysState`, `0` means screen off and `1` means screen on. `trigger` identifies the source of the screen-state change:

| `trigger` | Description |
| --- | --- |
| `phoneSetView` | Screen turned on by a phone-issued `SystemControl.setView` command |
| `notification` | Screen turned on by an incoming notification |
| `remoteDoubleClick` | Screen turned on by a phone-issued remote double-click |
| `forceDoubleClick` | Screen turned on by a temple Force double-click |
| `imuDoubleTap` | Screen turned on or off by an IMU double tap |
| `imuHeadUp` | Screen turned on by an IMU head-up gesture |
| `imuHeadDown` | Screen turned off by an IMU head-down gesture |
| `wearOn` | Screen turned on when wear detection reports that the glasses are worn |
| `keywordSpotting` | Screen turned on by keyword spotting |
| `inactivityTimeout` | Screen turned off after the inactivity timeout |
| `glassesCase` | Screen turned off when the glasses-case attachment is detected |

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
| `iconBitmap` | `bin` | No | Raw 32x32 L8 icon data, exactly 1024 bytes. |
| `iconBytes` | `bin` | No | Compatibility field for `iconBitmap`, used when `iconBitmap` is missing. It follows the same product behavior. |
| `level` | `uint8` | No | Notification level |
| `action` | `uint8` | No | Notification action type |
The glasses use the phone-provided icon when present and fall back to a built-in ROMFS icon.

`removeNotification`:

```msgpack
map(1) {
  "id": uint32(1001)
}
```

### 5.7 Toast

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `showToast` | `{ "text": string, "position": uint8, "postion": uint8, "duration": uint32 }` | `{}` |

`showToast.text` is required. `position` is optional: `1` top, `2` center, `3` bottom. The misspelled `postion` key is also accepted for compatibility. `duration` is optional in milliseconds; missing, zero, negative, or invalid values use the default `3000`.

### 5.8 TapMsgbox

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `showTapMsgbox` | `{ "title": string, "hint": string }` | `{}` |
| `showDownloadProgress` | `{ "title": string, "progress": 0..100 }` | `{}` |
| `closeTapMsgbox` | `{}` | `{}` |
| `closeDownloadProgress` | `{}` | `{}` |

Jytek does not support phone-controlled TapMsgbox prompts. All four commands above return `ErrNotReady`.

### 5.9 File

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

### 5.10 Draw

`Draw` manages up to 64 drawing items. A nonzero `id` identifies an item; sending another draw command with the same `id` updates or replaces that item. Common draw fields are:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | `uint32` | Yes | Nonzero drawing item ID |
| `x` / `y` | `int32` | Yes | Top-left display coordinates |
| `w` / `h` | `int32` | Yes | Drawing area width and height |
| `isfloat` | `bool` or `0/1` | No | `false` by default; when true, draw on the floating layer |

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `drawText` | Common fields plus `{ "text": string, "align"?: 0..3, "font_size"?: uint8, "isbolder"?: bool/0/1 }` | `{}` |
| `drawImage` | Common fields plus `{ "path": string }` | `{}` |
| `drawProgress` | Common fields plus `{ "progress": uint32 }` | `{}`; values above `100` are displayed as `100` |
| `updateProgress` | `{ "id": uint32, "progress": uint32 }` | `{}`; the ID must identify an existing progress bar, and values above `100` are displayed as `100` |
| `clearDrawId` | `{ "id": uint32 }` | `{}`; clearing an unknown ID also succeeds |
| `clearDrawAll` | `{}` | `{}` |

For `drawText`, `text` must be non-empty. `align` defaults to `0` and accepts `0=auto`, `1=left`, `2=center`, and `3=right`. `font_size=0` or omission uses the system font; any nonzero value must be a supported device font size. `isbolder` defaults to false and controls the text container border. `drawImage.path` must pass the device image-path validation.

Malformed fields return `ErrDataErr`. An invalid or zero ID, an unavailable drawing slot, or an invalid update target returns `ErrBadParam`.

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
| `id` | `uint32` | No | Record ID; defaults to `0` |
| `area` | `uint8` | No | Text area: `0` for primary content/answer, `1` for question, and `2` for centered guide prompt |
| `msgId` | `string` | No | Unique message ID. Used to update or remove the same record. |
| `msgType` | `uint8` | Yes | Text message type. It is written into the text buffer. |
| `actionType` | `uint8` | Yes | Text update action |
| `isFinal` | `uint8` | No | Whether this is the final result |
| `user` | `string` | No | Speaker or user |
| `transcribe` | `string` | No | Source text |
| `translate` | `string` | No | Translated text |
| `createdAt` | `uint64` | No | Unix timestamp in seconds |

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

| Value | Description |
| --- | --- |
| `0` | Hide the audio-track status icon |
| `1` | Show the audio-track status icon |

Other values return `ErrBadParam`.

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

#### `setHeaderText`

Displays phone-controlled prompt or result text at the top of the Speech page. It is independent of the body state and remains available on start, loading, and body views:

```msgpack
map(2) {
  "visible": uint8(1),
  "text": str("Prompt or result")
}
```

`visible` accepts `uint8` or `bool`. When showing the header, `text` is required and must not be empty. To hide it, send only `visible=0`. The header uses an opaque background over the body and does not participate in body layout, so toggling it does not move the body. The header remains until this command is called again, `clearView` is called, or the current Speech page exits. `setHeaderText` and `setLanguageHint` are mutually exclusive: showing the header hides the language hint, while a later `setLanguageHint` hides the header and shows the new language hint.

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
| `2` | Watch audio source |

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
| `setFunctionMenu` | See the 6.4 `setFunctionMenu` fields | `{}` |
| `setFontConfig` | See [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | See [6.1 Text Record](#61-text-record) | `{}` |
| `setState` | `{ "state": uint8 }`; only `0`/`1` are valid | `{}` |
| `setHeaderText` | `{ "visible": uint8/bool, "text"?: string }` | `{}` |
| `setTextMode` | `{ "textMode": uint8 }` | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` or `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setTransMode` | `{ "transMode": uint8 }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |
| `setLanguageHint` | See [6.2 setLanguageHint](#setlanguagehint) | `{}` |
| `textDirection` | See [6.2 textDirection](#textdirection) | `{}` |

### 6.4 Translate

Every Speech entry supports both `mode=0` single-language hints and `mode=1` dual-language hints through `setLanguageHint` and reuses the same Speech page implementation. With `reportdoubleclick=true`, a double-click is reported through `SystemInd.onTouchEvent` with `event=2` and the glasses do not exit. The phone may display the existing `TapMsgbox` through `TapMsgbox.showTapMsgbox` when confirmation is required. With `reportdoubleclick=false`, the glasses handle the double-click locally: `exitdoubleclick=true` displays the local exit confirmation dialog, while `exitdoubleclick=false` exits directly.

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setFunctionMenu` | See the `setFunctionMenu` fields below | `{}` |
| `setFontConfig` | See [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | See [6.1 Text Record](#61-text-record) | `{}` |
| `setState` | `{ "state": uint8 }`; only `0`/`1` are valid | `{}` |
| `setHeaderText` | `{ "visible": uint8/bool, "text"?: string }` | `{}` |
| `setTextMode` | `{ "textMode": uint8 }` | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` or `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setTransMode` | `{ "transMode": uint8 }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |
| `setLanguageHint` | See [6.2 setLanguageHint](#setlanguagehint) | `{}` |
| `textDirection` | See [6.2 textDirection](#textdirection) | `{}` |

`setFunctionMenu` displays a phone-controlled function-selection overlay on the Speech page:

```msgpack
map(3) {
  "menuId": uint32(1),
  "selectedItemId": uint32(100),
  "items": array(2) [
    { "id": uint32(100), "label": str("Mode A") },
    { "id": uint32(101), "label": str("Mode B") }
  ]
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `menuId` | `uint32` | Yes | Phone-defined menu instance or version ID |
| `selectedItemId` | `uint32` | Yes | Currently highlighted item ID; must match an `items[].id` |
| `items` | `array` | Yes | At least one item with unique IDs; the practical limit depends on packet size and available memory |
| `items[].id` | `uint32` | Yes | Phone-defined item ID |
| `items[].label` | `string` | Yes | Non-empty item label displayed verbatim on the glasses, up to 63 UTF-8 bytes |

While the menu is visible, the glasses do not change the highlighted item locally and do not emit a menu-specific selection message. Left swipe, right swipe, and click continue to use the existing `SystemInd.onTouchEvent` report with `event=6`, `event=7`, and `event=1`, respectively. The phone decides whether to select the previous item, select the next item, or confirm, and may send another `setFunctionMenu` command to refresh the highlight.

The roller reuses three display slots for the previous, current, and next items. Any other Speech command hides the menu. `SystemControl.setProgressVisible` with `visible=1` also hides it. The phone then updates the page through the applicable Speech command.

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

Prompter uses `id=5` and routes by `cmd`. With `reportdoubleclick=true`, a double-click is reported through `SystemInd.onTouchEvent` with `event=2`. With `reportdoubleclick=false`, the glasses handle it locally and exit the Prompter directly.

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `getTextLayout` | `{}` | `{ "breakAll": bool, "letterSpacePx": uint, "lineSpacePx": uint, "paddingHorizontalPx": uint, "paddingVerticalPx": uint, "textSizePx": uint, "totalHeightPx": uint, "totalWidthPx": uint }` |
| `setFontConfig` | See [6.2 setFontConfig](#setfontconfig) | `{}` |
| `setFileListMenu` | See `setFileListMenu` fields in this section | `{}` |
| `setPrompterFile` | `{ "dir": string, "name": string, "size": uint32, "crc32": uint32 }` | `{}` |
| `seekTo` | `{ "offset": uint32, "length": uint32, "topMaskHeight": uint32, "bottomMaskHeight": uint32, "duration"?: uint32 }` | `{}` |
| `setTick` | `{ "tick": uint32 }` | `{}` |
| `setState` | `{ "state": uint32 }`, `0` pause, `1` running | `{}` |

Prompter supports the optional `seekTo.duration` field in milliseconds. When omitted or set to `0`, the view jumps directly to the target text position without animation. When greater than `0`, adjacent text windows that can be joined scroll for the requested duration; non-contiguous windows still jump directly.

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

## 8. Recorder

Recorder uses `id=1002`, `biz=recorder`, and routes by `cmd`. Commands are handled only while Recorder is the current view; otherwise the device returns `ErrNotReady`.

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `setState` | `{ "state": uint32 }`; `0` pauses and shows the triangle, while `1` runs, shows the dot, and resets the time to `00:00:00` | `{}` |
| `setTick` | `{ "tick": uint32 }` in seconds | `{}` |

`setTick` formats the time as `HH:MM:SS`. While Running, even tick values show the dot and odd values hide it. While Paused, the triangle remains visible.

## 9. Gallery

Gallery uses `id=7` and routes by `cmd`.

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setGalleryFile` | `{ "dir": string, "name": string }` | `{}` |

## 10. Navigation

Navigation uses `id=4` and `biz=navigation`. Enter the `navigation` page through `SystemControl.setView` before sending business commands.

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}`; clears navigation data and shows the initial prompt |
| `updateNav` | Navigation object; see below | `{}` |
| `updateBpm` | `{ "bpm": string }` | `{}`; an empty string hides the heart-rate area |
| `updateSpo` | `{ "spo": string }` | `{}`; an empty string hides the blood-oxygen area |

`updateNav` fields:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `navMode` | `int32` | No | `1=driving`, `2=walking`, `3=cycling`; other values use the driving icon |
| `nextRoadName` | `string` | No | Next road name |
| `curStepRetainDistance` | `string` | No | Remaining distance in the current step |
| `remainDistance` | `string` | No | Total remaining distance |
| `remainTime` | `string` | No | Total remaining time |
| `speed` | `string` | No | Current speed; an empty string hides the speed area |
| `iconBytes` | `bin(2304)` | No | 48×48 L8 direction image; omission preserves the current image, and an invalid length is ignored |

## 11. ImageFusion

ImageFusion uses `id=1001`, `biz=imagefusion`, and `viewName=imagefusion` to read or set binocular display fusion offsets.

| cmd | Request `data` | Success ACK `data` |
| --- | --- | --- |
| `getFusionParams` | `{}` | `{ "lX": int8, "lY": int8, "rX": int8, "rY": int8 }` |
| `setFusionParams` | `{ "lX": int32, "lY": int32, "rX": int32, "rY": int32 }` | `nil` |

All four offset fields are required and must be within `-128..127`. Missing, mistyped, or out-of-range parameters return `ErrBadParam`; a low-level read or write failure returns `ErrDataErr`.

## 12. Maintenance Rules

When adding or changing a protocol, check the following:

1. Whether `id/type/error` in `common/message.h` needs an update.
2. Whether the target module has registered `app_message_t`.
3. Whether the target command is present in the corresponding `app_cmd_func_t` table.
4. Whether request `payload.data` remains `map` or `nil`.
5. Whether both success and failure paths return clear ACK/NACK.
6. Whether reports use `system_report_next_sequence()` or the module-specific sequence generator.
7. Whether this document has been updated.
