# Datapath V3 协议说明

英文版：[datapath_v3_protocol.md](datapath_v3_protocol.md)

主要代码入口：

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

## 1. 协议边界

`jy_app` 的公共入口 `app_mpack_msg_handle()` 接收的是一个完整 MsgPack 应用层报文。链路层如何分包、校验、重传或从队列中取出 payload，不属于本文范围。

应用层只关心以下结构：

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

字段说明：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `id` | `uint32` | 是 | 消息目标 ID，决定由哪个 app/system 处理 |
| `payload.seq` | `uint32` | 否 | 序列号；ACK/NACK 会原样带回 |
| `payload.type` | `uint8` | 否 | 消息类型，见下文 |
| `payload.cmd` | `string` | 是 | 命令名，不能为空 |
| `payload.biz` | `string` 或 `nil` | 否 | system 侧用于二级路由；普通 app 通常只按 `id + cmd` 路由 |
| `payload.data` | 按命令定义 | 否 | 请求方向要求为 `map` 或 `nil`；缺省或 `nil` 会传入 missing node，非 map 会返回 `ErrDataErr`；ACK/NACK 方向按命令返回字段定义 |

长度限制：

| 常量 | 值 | 说明 |
| --- | --- | --- |
| `MSG_BIZ_MAX_LEN` | 32 | `biz` 最大缓存长度，超长会被截断 |
| `MSG_CMD_MAX_LEN` | 32 | `cmd` 最大缓存长度，超长会被截断 |
| `MSG_STR_MAX_LEN` | 1024 | 公共字符串读取缓存长度 |

## 2. 消息类型

| 常量 | 值 | 方向 | 说明 |
| --- | --- | --- | --- |
| `MSG_TYPE_DATA_UNRELIABLE` | `0x00` | 双向 | 普通数据消息；是否回 ACK 取决于业务处理函数 |
| `MSG_TYPE_ACK` | `0x01` | 双向 | 成功确认 |
| `MSG_TYPE_NAK` | `0x02` | 双向 | 错误确认 |
| `MSG_TYPE_HANDSHAKE` | `0x03` | 双向 | 握手类型，公共解析保留 type 值 |
| `MSG_TYPE_HEARTBEAT` | `0x04` | 双向 | 心跳类型，公共解析保留 type 值 |
| `MSG_TYPE_DATA_RELIABLE` | `0x80` | 双向 | 可靠数据消息，业务通常需要返回 ACK/NACK |
| `MSG_TYPE_INVALID` | `0xFF` | 内部 | 初始化占位值 |

公共层不会只因为 `type` 是 `DATA_UNRELIABLE` 就禁止 ACK。实际是否回复由命令处理函数决定。

## 3. ACK / NACK

公共发送函数 `app_mpack_create_writer()` 会复用原报文的 `id/seq/cmd/biz`，只替换 `payload.type` 和 `payload.data`。

成功 ACK：

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

错误 NACK：

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

错误码：

| code | 常量 | 说明 |
| --- | --- | --- |
| 0 | `Dp_ErrNone` | 成功 |
| 1 | `ErrBizErr` | 业务错误 |
| 2 | `ErrCmdErr` | 命令错误或未知 `biz/cmd` |
| 3 | `ErrIDErr` | `id` 未注册 |
| 4 | `ErrNameErr` | 名称错误 |
| 5 | `ErrPayloadErr` | payload 错误 |
| 6 | `ErrSeqErr` | 序列号错误 |
| 7 | `ErrTypeErr` | 类型错误 |
| 8 | `ErrDataErr` | 数据错误 |
| 9 | `ErrBadParam` | 参数错误 |
| 10 | `ErrDataTypeMismatch` | 数据类型不匹配 |
| 11 | `ErrNotReady` | 当前状态不允许处理 |
| 12 | `ErrCmdNotImplemented` | 命令未实现 |
| 13 | `ErrFontNotExistFailed` | 字体不存在 |
| 14 | `ErrFileNotExistFailed` | 文件不存在 |
| 15 | `ErrBadFilePath` | 文件路径错误 |
| 16 | `ErrBtErr` | 蓝牙错误 |
| 17 | `ErrBadCRC` | CRC 错误 |

注意：`ErrBadCRC` 没有专用文案；如果返回该错误码，`msg` 会落到 `"Unknown Error"`。

## 4. 消息 ID

| id | 常量 | 模块 |
| --- | --- | --- |
| 0 | `APP_MSG_ID_SYSTEM` | System |
| 2 | `APP_MSG_ID_TRANSCRIBE` | Transcribe |
| 3 | `APP_MSG_ID_TRANSLATE` | Translate |
| 5 | `APP_MSG_ID_PROMPTER` | Prompter |
| 7 | `APP_MSG_ID_GALLERY` | Gallery |
| 8 | `APP_MSG_ID_AI` | AI |

公共处理流程：

1. 解析根节点 `id`。
2. 解析 `payload.seq/type/cmd/biz`。
3. 如果当前顶层 app 声明消费 host message，则优先交给该 app。
4. 如果蓝牙断连遮罩或 popup 状态不允许处理，则返回 `ErrNotReady`。
5. 校验 `data`：缺省或 `nil` 允许；非 map 返回 `ErrDataErr`。
6. 按 `id` 查找注册的 `app_message_t`。
7. 调用目标模块路由函数。
8. 处理成功后刷新睡眠计时器；通知类特殊消息可抑制该刷新。

## 5. System 协议

System 使用 `id=0`，并按 `payload.biz` 二级路由。

| biz | 文件 | 说明 |
| --- | --- | --- |
| `DeviceInfo` | `system_msg_devinfo.c` | 设备信息查询 |
| `SystemConfig` | `system_msg_sysconfig.c` | 系统配置查询和设置 |
| `SystemStatus` | `system_msg_sysstatus.c` | 运行时状态查询和少量状态控制 |
| `SystemControl` | `system_msg_syscontrol.c` | 系统控制、切 view、assistant、触控注入 |
| `SystemInd` | `system_msg_sysind.c` | 接收远端心跳、保活、关键词响应 |
| `Notification` | `system_notification.c` | 通知增删改 |
| `File` | `system_msg_file.c` | 文件列表、写入、删除、存在性和清目录 |

未知 `biz` 或未知 `cmd` 都返回 `ErrCmdErr`。

### 5.1 DeviceInfo

| cmd | 请求 `data` | 成功 ACK `data` |
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

| cmd | 请求 `data` | ACK / NACK `data` |
| --- | --- | --- |
| `getAll` | `{}` | `{ "time": uint64, "timeConfig": { "time": string, "timestamp": uint64, "timezone": string, "userFormat": string }, "displayConfig": { "mode": uint8 }, "brightness": uint8, "autoBrightnessEnabled": uint8, "fontSize": uint8, "language": string, "inactivityTimeout": uint16, "poweroffTimeout": uint16, "wearDetectionEnabled": uint8, "headGestureConfig": { "upEnabled": uint8, "downEnabled": uint8, "upDeg": int32, "downDeg": int32, "baseDeg": int32 }, "touchpadEnabled": uint8, "idleDetectionEnabled": uint8, "displayDistanceLevel": uint32, "keywordSpottingEnabled": uint8, "notificationEnabled": uint8 }` |
| `setTime` | `{ "time": uint32 }` | NACK `{ "code": 12, "msg": string }` |
| `getTimeConfig` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `setTimeConfig` | `{ "time": string, "timestamp": uint64, "timezone": string, "userFormat": string }`；`timezone` 可缺省 | `{}` |
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
| `setDisplayDistanceLevel` | `{ "displayDistanceLevel": uint32 }`，取值 `1..3` | `{}` |
| `getDisplayDistance` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `setDisplayDistance` | `{ "distance": uint32 }` | `{}` |
| `getDisplayPopupDepth` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `setDisplayPopupDepth` | `{ "depth": uint32 }` | `{}` |
| `getInactivityTimeout` | `{}` | `{ "inactivityTimeout": uint16 }` |
| `setInactivityTimeout` | `{ "inactivityTimeout": uint32 }` | `{}` |
| `getPoweroffTimeout` | `{}` | `{ "poweroffTimeout": uint16 }` |
| `setPoweroffTimeout` | `{ "poweroffTimeout": uint32 }` | `{}` |
| `getHeadGestureConfig` | `{}` | `{ "upEnabled": uint8, "downEnabled": uint8, "upDeg": int32, "downDeg": int32, "baseDeg": int32 }` |
| `setHeadGestureConfig` | `{ "upEnabled": uint, "downEnabled": uint, "upDeg": int32, "downDeg": int32, "baseDeg": int32 }`；角度字段可缺省 | `{}` |
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

`setTimeConfig.time` 使用 `yyyy-MM-dd HH:mm:ss` 格式。开关字段用 `0/1` 表示关闭/打开。

### 5.3 SystemStatus

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `getAll` | `{}` | `{ "sysState": uint8, "chargeState": uint8, "battery": uint8 }` |
| `getSysState` | `{}` | `{ "sysState": uint8 }` |
| `setSysState` | `{ "sysState": uint8 }` | `{}` |
| `getChargeState` | `{}` | `{ "chargeState": uint8 }` |
| `getBattery` | `{}` | `{ "battery": uint8 }` |
| `getRomUsage` | `{}` | `{ "total": uint32, "used": uint32, "remaining": uint32 }` |

### 5.4 SystemControl

| cmd | 请求 `data` | ACK / NACK `data` |
| --- | --- | --- |
| `unbind` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `factoryReset` | `{}` | `{}` |
| `reboot` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `recovery` | `{}` | NACK `{ "code": 12, "msg": string }` |
| `getView` | `{}` | `{ "view": string }` |
| `setView` | `{ "viewName": string }` | `{}` |
| `sendTouchEvent` | `{ "event": uint8 }` | `{}` |
| `openAssistant` | `{}` | `{}` |
| `updateAssistantSttInfo` | 见 [6.1 文本记录](#61-文本记录) | `{}` |
| `closeAssistant` | `{}` | `{}` |
| `sendHeartbeat` | `{}` | `{}` |
| `sendKeepAlive` | `{}` | `{}` |
| `sendHandshake` | `{}` | `{}` |

`sendTouchEvent.event`：

| 值 | 说明 |
| --- | --- |
| `0x01` | 单击 |
| `0x02` | 双击 |
| `0x03` | 长按 |
| `0x04` | 上滑 |
| `0x05` | 下滑 |
| `0x06` | 左滑 |
| `0x07` | 右滑 |

Assistant 的文本字段见 [6.6 Assistant Popup](#66-assistant-popup)。

### 5.5 SystemInd

接收方向：

| cmd | 接收 `payload.data` | 回包 |
| --- | --- | --- |
| `heartBeat` | `{}` | `{}` |
| `keepAlive` | `{}` | `{}` |
| `onKeywordSpotting` | ACK `{}` 或 NACK `{ "code": uint32, "msg": string }` | 不再二次回包 |

眼镜主动上报方向：

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

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `addNotification` | `{ "id": uint32, "type": uint8, "title": string, "msg": string, "duration": uint32, "iconBitmap": bin, "iconBytes": bin, "level": uint8, "action": uint8 }` | `{}` |
| `updateNotification` | `{ "id": uint32, "type": uint8, "title": string, "msg": string, "duration": uint32, "iconBitmap": bin, "iconBytes": bin, "level": uint8, "action": uint8 }` | `{}` |
| `removeNotification` | `{ "id": uint32 }` | `{}` |

`addNotification` 和 `updateNotification` 使用相同的 `data` 字段：

```msgpack
map(8) {
  "id": uint32(1001),
  "type": uint8(1),
  "title": str("新消息"),
  "msg": str("您有一条通知"),
  "duration": uint32(3),
  "iconBitmap": bin(1024),
  "level": uint8(1),
  "action": uint8(1)
}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `id` | `uint32` | 是 | 通知 ID |
| `type` | `uint8` | 否 | 通知类型；`2` 为来电，其它值按普通消息处理 |
| `title` | `string` | 否 | 通知标题 |
| `msg` | `string` | 否 | 通知正文 |
| `duration` | `uint32` 或非负 `int32` | 否 | 自动关闭时间，单位秒；缺省使用默认时长，来电通知不自动关闭 |
| `iconBitmap` | `bin` | 否 | 32x32 L8 原始图标数据，长度为 1024 字节 |
| `iconBytes` | `bin` | 否 | `iconBitmap` 的兼容字段；当 `iconBitmap` 缺省时使用 |
| `level` | `uint8` | 否 | 通知等级 |
| `action` | `uint8` | 否 | 通知动作类型 |

`title`、`msg` 和图标至少要有一个可展示内容；没有传图标时会使用默认图标。

`removeNotification`：

```msgpack
map(1) {
  "id": uint32(1001)
}
```

### 5.7 File

文件类型：

| 值 | 常量 | 说明 |
| --- | --- | --- |
| `0` | `SYSTEM_FILE_TYPE_DIR` | 目录 |
| `1` | `SYSTEM_FILE_TYPE_FILE` | 文件 |
| `2` | `SYSTEM_FILE_TYPE_OTHER` | 其它类型，不作为有效写入/删除类型 |

| cmd | 请求 `data` | ACK / NACK `data` |
| --- | --- | --- |
| `getFileList` | `{ "dir": string }`；`dir` 缺省时使用文件系统根目录 | 成功返回数组：`[{ "dir": string, "name": string, "type": uint8, "size": uint32, "crc32": uint32 }]`；空目录返回 NACK `{ "code": 14, "msg": string }` |
| `writeFile` | 写目录：`{ "type": 0, "dir": string }`；写文件见本节写文件请求结构 | `{}` |
| `writeFileByBinary` | 与 `writeFile` 相同 | `{}` |
| `removeFile` | 删目录：`{ "type": 0, "dir": string }`；删文件：`{ "type": 1, "dir": string, "name": string }` | `{}` |
| `isFileExist` | 查目录：`{ "type": 0, "dir": string }`；查文件：`{ "type": 1, "dir": string, "name": string, "size": uint32, "crc32": uint32 }` | `{}` |
| `clearFolder` | `{ "type": 0, "dir": string }` | `{}` |

`writeFile` / `writeFileByBinary` 写文件请求：

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

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `type` | `uint32` | 是 | `1` 表示文件，`0` 表示目录 |
| `dir` | `string` | 是 | 目录路径 |
| `name` | `string` | 写文件时必填 | 文件名 |
| `size` | `uint32` | 写文件时必填 | 完整文件大小 |
| `crc32` | `uint32` | 写文件时必填 | 完整文件 CRC 值 |
| `pkt.total` | `uint32` | 是 | 总分包数，必须大于 0 |
| `pkt.cur` | `uint32` | 是 | 当前分包序号，从 1 开始，不能大于 `total` |
| `pkt.crc32` | `uint32` | 是 | 当前分包数据 CRC 值 |
| `pkt.bytes` | `bin` 或 `string` | 是 | 当前分包内容，长度必须大于 0 且不超过 65535 字节 |

文件命令会做路径合法性、文件存在性和 CRC 校验，常见错误为 `ErrBadFilePath`、`ErrFileNotExistFailed`、`ErrBadCRC`。

## 6. STT / AI 文本协议

STT、翻译、AI 文本和 Assistant popup 使用同一组文本字段。

| id | biz | 模块 |
| --- | --- | --- |
| 2 | `transcribe` | 转写 |
| 3 | `translate` | 翻译 |
| 8 | `ai` | AI |
| 0 | `SystemControl` | Assistant popup |

### 6.1 文本记录

`updateSttInfo` 和 `updateAssistantSttInfo` 的 `data` 为单条文本记录：

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
  "translate": str("你好世界"),
  "createdAt": uint64(1678888888)
}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `id` | `uint32` | 是 | 记录 ID |
| `area` | `uint8` | 是 | 文本区域；Assistant 中 `0` 为回答、`1` 为提问、`2` 为引导提示 |
| `msgId` | `string` | 否 | 消息唯一 ID，用于更新或删除同一条记录 |
| `msgType` | `uint8` | 是 | 文本消息类型，会写入文本缓冲 |
| `actionType` | `uint8` | 是 | 文本更新动作 |
| `isFinal` | `uint8` | 是 | 是否最终结果 |
| `user` | `string` | 否 | 说话人或用户 |
| `transcribe` | `string` | 否 | 原文 |
| `translate` | `string` | 否 | 译文 |
| `createdAt` | `uint64` | 是 | Unix 秒级创建时间戳 |

`actionType`：

| 值 | 行为 |
| --- | --- |
| `0` | set / replace；若存在同 `msgId` 记录则覆盖，否则写到缓冲头部 |
| `1` | append / upsert；若存在同 `msgId` 记录则更新，否则写到缓冲头部 |
| `2` | remove；按 `msgId` 删除 |

文本缓冲最多保留 6 条记录。`msgId` 为空时，按 `msgId` 更新或删除的能力会受限。

### 6.2 公共命令

本节命令成功 ACK 的 `data` 均为 `{}`。

#### `clearView`

```msgpack
map(0) {}
```

清空文本缓冲和页面文本。

#### `setFontConfig`

支持直接传字体字段：

```msgpack
map(3) {
  "weight": uint32(30),
  "wordSpace": uint32(4),
  "rowSpace": uint32(4)
}
```

也支持包在 `fontConfig` 中：

```msgpack
map(1) {
  "fontConfig": map(3) {
    "weight": uint32(30),
    "wordSpace": uint32(4),
    "rowSpace": uint32(4)
  }
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `weight` | `uint32` | 字号配置值 |
| `wordSpace` | `uint32` | 字距 |
| `rowSpace` | `uint32` | 行距 |

#### `setTextMode`

```msgpack
map(1) {
  "textMode": uint8(1)
}
```

| 值 | 说明 |
| --- | --- |
| `0` | 默认模式 |
| `1` | 历史模式 |
| `2` | 会议模式 |

#### `setAudioTrackState`

支持直接传音轨字段：

```msgpack
map(1) {
  "audioTrack": uint8(1)
}
```

也支持包在 `data` 中：

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

| 值 | 说明 |
| --- | --- |
| `0` | 仅显示译文 |
| `1` | 原文 + 译文 |
| `2` | 仅显示原文 |

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

| 值 | 说明 |
| --- | --- |
| `0` | 眼镜音源 |
| `1` | 手机音源 |

#### `setMicDirectional`

```msgpack
map(1) {
  "micDirectional": uint8(1)
}
```

| 值 | 说明 |
| --- | --- |
| `0` | 全向 |
| `1` | 指向 |

#### `setLanguageHint`

支持直接传语言字段：

```msgpack
map(3) {
  "mode": uint8(1),
  "source": str("en-US"),
  "target": str("zh-CN")
}
```

也支持包在 `languageHint` 中：

```msgpack
map(1) {
  "languageHint": map(3) {
    "mode": uint8(1),
    "source": str("en-US"),
    "target": str("zh-CN")
  }
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `mode` | `uint8` | `0` 单语言提示；`1` 双语言提示 |
| `source` | `string` | 源语言 |
| `target` | `string` | 目标语言；`mode=1` 时必填 |

#### `textDirection`

支持直接传方向字段：

```msgpack
map(2) {
  "source": uint8(0),
  "target": uint8(1)
}
```

也支持包在 `textDirection` 中：

```msgpack
map(1) {
  "textDirection": map(2) {
    "source": uint8(0),
    "target": uint8(1)
  }
}
```

| 值 | 说明 |
| --- | --- |
| `0` | 从左到右 |
| `1` | 从右到左 |

`target` 可缺省；缺省时跟随 `source`。

### 6.3 Transcribe

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setFontConfig` | 见 [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | 见 [6.1 文本记录](#61-文本记录) | `{}` |
| `setTextMode` | `{ "textMode": uint8 }` | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` 或 `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setTransMode` | `{ "transMode": uint8 }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |
| `setLanguageHint` | 见 [6.2 setLanguageHint](#setlanguagehint) | `{}` |
| `textDirection` | 见 [6.2 textDirection](#textdirection) | `{}` |

### 6.4 Translate

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setFontConfig` | 见 [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | 见 [6.1 文本记录](#61-文本记录) | `{}` |
| `setTextMode` | `{ "textMode": uint8 }` | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` 或 `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setTransMode` | `{ "transMode": uint8 }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |
| `setLanguageHint` | 见 [6.2 setLanguageHint](#setlanguagehint) | `{}` |
| `textDirection` | 见 [6.2 textDirection](#textdirection) | `{}` |

### 6.5 AI

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setFontConfig` | 见 [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | 见 [6.1 文本记录](#61-文本记录) | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` 或 `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |

### 6.6 Assistant Popup

Assistant popup 挂在 `SystemControl` 下：

| id | biz | cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- | --- | --- |
| 0 | `SystemControl` | `openAssistant` | `{}` | `{}` |
| 0 | `SystemControl` | `updateAssistantSttInfo` | 见 [6.1 文本记录](#61-文本记录) | `{}` |
| 0 | `SystemControl` | `closeAssistant` | `{}` | `{}` |

`updateAssistantSttInfo` 的文本记录字段含义与本章文本记录一致。Assistant 中 `area=1` 作为提问，`area=0` 作为回答，`area=2` 作为引导提示。

## 7. Prompter

Prompter 使用 `id=5`，按 `cmd` 路由。

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `getTextLayout` | `{}` | `{ "breakAll": bool, "letterSpacePx": uint, "lineSpacePx": uint, "paddingHorizontalPx": uint, "paddingVerticalPx": uint, "textSizePx": uint, "totalHeightPx": uint, "totalWidthPx": uint }` |
| `setFontConfig` | 见 [6.2 setFontConfig](#setfontconfig) | `{}` |
| `setFileListMenu` | 见本节 `setFileListMenu` 字段 | `{}` |
| `setPrompterFile` | `{ "dir": string, "name": string, "size": uint32, "crc32": uint32 }` | `{}` |
| `seekTo` | `{ "offset": uint32, "length": uint32, "topMaskHeight": uint32, "bottomMaskHeight": uint32 }` | `{}` |
| `setTick` | `{ "tick": uint32 }` | `{}` |
| `setState` | `{ "state": uint32 }`，`0` 暂停，`1` 运行 | `{}` |

`setFileListMenu`：

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

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `menuId` | `uint32` | 是 | 菜单 ID |
| `itemCount` | `uint32` | 是 | 菜单项数量 |
| `defaultItemId` | `uint32` | 否 | 默认选中的菜单项 ID |
| `items` | `array` | `itemCount > 0` 时必填 | 菜单项数组，长度必须等于 `itemCount` |
| `items[].id` | `uint32` | 是 | 菜单项 ID |
| `items[].label` | `string` | 是 | 菜单项显示文本 |

Prompter 会主动上报：

| cmd | type | data |
| --- | --- | --- |
| `onMenuSelected` | `DATA_RELIABLE` | `{ "menuId": uint, "selectedItemId": uint }` |

## 8. Gallery

Gallery 使用 `id=7`，按 `cmd` 路由。

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setGalleryFile` | `{ "dir": string, "name": string }` | `{}` |

## 9. 维护规则

新增或修改协议时，按以下顺序检查：

1. `common/message.h` 中的 `id/type/error` 是否需要更新。
2. 目标模块是否已注册 `app_message_t`。
3. 目标命令是否在对应 `app_cmd_func_t` 表中。
4. 请求方向的 `payload.data` 是否保持 map 或 nil。
5. 成功路径和失败路径是否都返回明确 ACK/NACK。
6. 主动上报是否使用 `system_report_next_sequence()` 或模块对应的 sequence 生成方式。
7. 文档是否同步更新。
