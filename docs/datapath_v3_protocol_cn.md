# JYTek Datapath V3 协议说明

英文版：[datapath_v3_protocol.md](datapath_v3_protocol.md)

主要代码入口：

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
| 18 | `ErrScreenOff` | 设备息屏，命令被拒绝 |
| 19 | `ErrGuideStepMismatch` | 命令不允许在当前新手引导步骤处理 |

## 4. 消息 ID

| id | 常量 | 模块 |
| --- | --- | --- |
| 0 | `APP_MSG_ID_SYSTEM` | System |
| 1 | `APP_MSG_ID_HOME` | Home；无独立业务命令 |
| 2 | `APP_MSG_ID_TRANSCRIBE` | Transcribe |
| 3 | `APP_MSG_ID_TRANSLATE` | Translate |
| 4 | `APP_MSG_ID_NAVIGATION` | Navigation |
| 5 | `APP_MSG_ID_PROMPTER` | Prompter |
| 7 | `APP_MSG_ID_GALLERY` | Gallery |
| 8 | `APP_MSG_ID_AI` | AI |
| 13 | `APP_MSG_ID_GUIDE` | Guide；由 `SystemControl` 控制 |
| 1001 | `APP_MSG_ID_IMAGEFUSION` | ImageFusion |
| 1002 | `APP_MSG_ID_RECORDER` | Recorder |

公共处理流程：

1. 解析根节点 `id`。
2. 解析 `payload.seq/type/cmd/biz`。
3. 如果当前处于息屏状态且命令不在息屏白名单内，则返回 `ErrScreenOff`。
4. 如果当前顶层 app 声明消费 host message，则优先交给该 app。
5. 如果蓝牙断连遮罩或 popup 状态不允许处理，则返回 `ErrNotReady`。
6. 如果新手引导进行中且命令和当前引导步骤无关，则返回 `ErrGuideStepMismatch`。
7. 校验 `data`：缺省或 `nil` 允许；非 map 返回 `ErrDataErr`。
8. 按 `id` 查找注册的 `app_message_t`。
9. 调用目标模块路由函数。
10. 处理成功后刷新睡眠计时器；通知类特殊消息可抑制该刷新。

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
| `Toast` | `system_msg_toast.c` | Toast 弹窗显示 |
| `TapMsgbox` | `system_msg_tap_msgbox.c` | 确认框和下载进度 |
| `File` | `system_msg_file.c` | 文件列表、写入、删除、存在性和清目录 |
| `Draw` | `system_msg_draw.c` | 在设备屏幕绘制文字、图片和进度条 |

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
| `getAll` | `{}` | `{ "time": uint64, "timeConfig": { "time": string, "timestamp": uint64, "timezone": string, "userFormat": string }, "displayConfig": { "mode": uint8 }, "brightness": uint8, "autoBrightnessEnabled": uint8, "fontSize": uint8, "language": string, "homeUnits": string[], "inactivityTimeout": uint16, "poweroffTimeout": uint16, "wearDetectionEnabled": uint8, "headGestureConfig": { "upEnabled": uint8, "downEnabled": uint8, "upDeg": int32, "downDeg": int32, "baseDeg": int32 }, "imuDoubleTapEnabled": uint8, "touchpadEnabled": uint8, "idleDetectionEnabled": uint8, "displayDistanceLevel": uint32, "keywordSpottingEnabled": uint8, "notificationEnabled": uint8 }` |
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
| `getHomeUnits` | `{}` | `{ "homeUnits": string[] }` |
| `setHomeUnits` | `{ "homeUnits": string[] }` | `{}` |
| `getHomeMenuConfig` | `{}` | `{ "all": uint32[], "visible": uint32[], "selected": uint32 }` |
| `setHomeMenuConfig` | `{ "visible": uint32[], "selected": uint32 }` | `{}`；`selected` 必须包含在 `visible` 中 |
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

`setTimeConfig.time` 使用 `yyyy-MM-dd HH:mm:ss` 格式。开关字段用 `0/1` 表示关闭/打开。`autoBrightnessEnabled` 开启时，`setBrightness` 返回 NACK `ErrNotReady`，且不会修改 LCD 亮度。

Jytek 首页协议单元为 `prompter(5)`、`translate(3)`、`transcribe(2)`、`ai(8)` 和 `navigation(4)`。

### 5.3 SystemStatus

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `getAll` | `{}` | `{ "sysState": uint8, "chargeState": uint8, "battery": uint8 }` |
| `getSysState` | `{}` | `{ "sysState": uint8 }` |
| `setSysState` | `{ "sysState": uint8 }` | `{}` |
| `getChargeState` | `{}` | `{ "chargeState": uint8 }` |
| `getBattery` | `{}` | `{ "battery": uint8 }` |
| `getAttachmentState` | `{}` | `{ "attachments": [{ "attachmentSide": uint8, "attachmentType": uint8 }] }` |
| `getRomUsage` | `{}` | `{ "total": uint32, "used": uint32, "remaining": uint32 }` |

`getAttachmentState.attachments` 固定包含主侧和从侧两项，返回 `jy_app` 最近一次收到并保存在内存中的附件类型。`attachmentSide` 与 `attachmentType` 的取值见 [5.5 SystemInd](#55-systemind) 的 `onAttachmentTypeChanged` 定义。

`getRomUsage` 统计 `/jyt_d` 所在 LFSD 文件系统，三个容量字段的单位均为字节。

### 5.4 SystemControl

| cmd | 请求 `data` | ACK / NACK `data` |
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
| `updateAssistantSttInfo` | 见 [6.1 文本记录](#61-文本记录) | `{}` |
| `closeAssistant` | `{}` | `{}` |
| `sendHeartbeat` | `{}` | `{}` |
| `sendKeepAlive` | `{}` | `{}` |
| `sendHandshake` | `{}` | `{}` |
| `setProgressVisible` | 显示：`{ "visible": 1, "text": string, "opa": 0..100 }`；隐藏：`{ "visible": 0 }` | `{}` |
| `setUploadProgressVisible` | `{ "visible": bool/0/1 }` | 仅当前页面支持上传进度且文件正在传输时成功；Jytek 的 `prompter` 和 `gallery` 支持 |

`factoryReset` 会在 `/jyt_d` 写入并同步清理标记，随后保留该标记并删除其余用户数据，不再从 ROMFS 回拷文件。无论本次清理成功或失败，设备都会先关闭屏幕再触发 assert 复位，不保证返回 ACK；失败时标记会保留。下次启动发现标记时，会在加载系统配置前继续清理；若仍然失败则继续 assert 复位并在后续启动时重试。全部用户数据删除成功后才移除标记，配置随后由 ROMFS 默认值与新的 LFSD 稀疏覆盖共同生成。

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

`getScreenOnScope` 返回当前内存中的临时常亮范围。`setScreenOnScope.scope` 仅修改该内存状态，不修改持久化的无操作灭屏配置：

| 值 | 说明 |
| --- | --- |
| `0` | 关闭临时常亮，并从当前时刻重新起算无操作灭屏时间 |
| `1` | 当前 App 内临时常亮；离开当前 App 或手机断连时恢复无操作灭屏 |
| `2` | 本次手机连接内临时常亮；手机断连时恢复无操作灭屏 |

Assistant 的文本字段见 [6.6 Assistant Popup](#66-assistant-popup)。

`openGuide` 会将 `SystemConfig.userguide` 写回 `"false"` 并切到 Guide 应用；`closeGuide` 会重置新手引导运行态，将 `SystemConfig.userguide` 写回 `"true"`，返回 Home，并上报 `viewName="home"` 的 `onViewChangedByName`。手机端触发的 `openGuide` 和 `closeGuide` 不会上报 `onGuideOpen` 或 `onGuideClose`。眼镜端自己进入新手引导时上报 `onGuideOpen`；眼镜端完成或跳过新手引导时先上报 `onGuideClose`，再上报 `viewName="home"` 的 `onViewChangedByName`。Guide 内部流转不会上报 `onViewChangedByName`。

### 5.5 SystemInd

接收方向：

| cmd | 接收 `payload.data` | 回包 |
| --- | --- | --- |
| `heartBeat` | `{}` | `{}` |
| `keepAlive` | `{}` | `{}` |
| `onKeywordSpotting` | ACK `{}` 或 NACK `{ "code": uint32, "msg": string }` | 不再二次回包 |
| `onGuideOpen` | `{}` | `{}` |
| `onGuideClose` | `{}` | `{}` |

眼镜主动上报方向：

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

`onAttachmentTypeChanged.attachmentType`：

| 值 | 说明 |
| --- | --- |
| `0` | 无附件 |
| `1` | 眼镜盒 |
| `2` | 外接电源 |
| `3` | 扬声器 |

`onAttachmentTypeChanged.attachmentSide`：

| 值 | 说明 |
| --- | --- |
| `0` | 主侧（当前硬件对应右侧） |
| `1` | 从侧（当前硬件对应左侧） |

每条事件只更新 `attachmentSide` 指定的一侧。接收方应分别保存两侧的最新类型；`attachmentType` 为 `0` 仅表示该侧已摘除，不表示所有附件均已摘除。

`onSysStateChanged.sysState`：`0` 表示灭屏，`1` 表示亮屏。`trigger` 表示本次亮灭屏状态变化的触发来源：

| `trigger` | 说明 |
| --- | --- |
| `phoneSetView` | 手机下发 `SystemControl.setView` 时触发亮屏 |
| `notification` | 通知到达时触发亮屏 |
| `remoteDoubleClick` | 手机下发远程双击时触发亮屏 |
| `forceDoubleClick` | 镜腿 Force 双击时触发亮屏 |
| `imuDoubleTap` | IMU 双击触发亮屏或灭屏 |
| `imuHeadUp` | IMU 抬头触发亮屏 |
| `imuHeadDown` | IMU 低头触发灭屏 |
| `wearOn` | 佩戴检测到戴上时触发亮屏 |
| `keywordSpotting` | 关键词唤醒触发亮屏 |
| `inactivityTimeout` | 无操作超时触发灭屏 |
| `glassesCase` | 收到眼镜盒附件时触发灭屏 |

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
| `iconBitmap` | `bin` | 否 | 32×32 L8 原始图标，长度必须为 1024 字节 |
| `iconBytes` | `bin` | 否 | `iconBitmap` 的兼容字段；缺少 `iconBitmap` 时使用，产品行为相同 |
| `level` | `uint8` | 否 | 通知等级 |
| `action` | `uint8` | 否 | 通知动作类型 |
眼镜优先显示手机下发图标，缺省时使用 ROMFS 内置兜底图标。

`removeNotification`：

```msgpack
map(1) {
  "id": uint32(1001)
}
```

### 5.7 Toast

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `showToast` | `{ "text": string, "position": uint8, "postion": uint8, "duration": uint32 }` | `{}` |

`showToast.text` 必填。`position` 可缺省：`1` 顶部，`2` 中间，`3` 底部。为兼容示例里的拼写，也接受 `postion` 字段。`duration` 可缺省，单位毫秒；缺省、0、负数或非法值统一使用默认 `3000`。

### 5.8 TapMsgbox

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `showTapMsgbox` | `{ "title": string, "hint": string }` | `{}` |
| `showDownloadProgress` | `{ "title": string, "progress": 0..100 }` | `{}` |
| `closeTapMsgbox` | `{}` | `{}` |
| `closeDownloadProgress` | `{}` | `{}` |

Jytek 不支持手机端控制的 TapMsgbox 提示框，上述四个命令均返回 `ErrNotReady`。

### 5.9 File

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

### 5.10 Draw

`Draw` 最多管理 64 个绘制项。非零 `id` 用于标识绘制项；使用相同 `id` 再次发送绘制命令时，会更新或替换该绘制项。公共绘制字段如下：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `id` | `uint32` | 是 | 非零绘制项 ID |
| `x` / `y` | `int32` | 是 | 左上角显示坐标 |
| `w` / `h` | `int32` | 是 | 绘制区域宽度和高度 |
| `isfloat` | `bool` 或 `0/1` | 否 | 缺省为 `false`；为 true 时绘制到悬浮层 |

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `drawText` | 公共字段，加 `{ "text": string, "align"?: 0..3, "font_size"?: uint8, "isbolder"?: bool/0/1 }` | `{}` |
| `drawImage` | 公共字段，加 `{ "path": string }` | `{}` |
| `drawProgress` | 公共字段，加 `{ "progress": uint32 }` | `{}`；大于 `100` 的值按 `100` 显示 |
| `updateProgress` | `{ "id": uint32, "progress": uint32 }` | `{}`；ID 必须对应已有进度条，大于 `100` 的值按 `100` 显示 |
| `clearDrawId` | `{ "id": uint32 }` | `{}`；清除未知 ID 也成功 |
| `clearDrawAll` | `{}` | `{}` |

`drawText.text` 必须为非空字符串。`align` 缺省为 `0`，支持 `0=自动`、`1=左对齐`、`2=居中`、`3=右对齐`。`font_size=0` 或不提供时使用系统字体；非零值必须是设备支持的字号。`isbolder` 缺省为 false，用于控制文字容器边框。`drawImage.path` 必须通过设备图片路径校验。

字段缺失或格式错误返回 `ErrDataErr`；ID 为零或无效、绘制槽位不足、进度更新目标无效时返回 `ErrBadParam`。

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
| `id` | `uint32` | 否 | 记录 ID，缺省为 `0` |
| `area` | `uint8` | 否 | 文本区域；`0` 为主要内容/回答、`1` 为提问、`2` 为居中引导提示 |
| `msgId` | `string` | 否 | 消息唯一 ID，用于更新或删除同一条记录 |
| `msgType` | `uint8` | 是 | 文本消息类型，会写入文本缓冲 |
| `actionType` | `uint8` | 是 | 文本更新动作 |
| `isFinal` | `uint8` | 否 | 是否最终结果 |
| `user` | `string` | 否 | 说话人或用户 |
| `transcribe` | `string` | 否 | 原文 |
| `translate` | `string` | 否 | 译文 |
| `createdAt` | `uint64` | 否 | Unix 秒级创建时间戳 |

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

| 值 | 说明 |
| --- | --- |
| `0` | 隐藏音轨状态图标 |
| `1` | 显示音轨状态图标 |

其他值返回 `ErrBadParam`。

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

#### `setHeaderText`

在 Speech 页面顶部显示由手机控制的提示或结果文本。该文本独立于正文页面状态，起始页、加载页和正文页均可显示：

```msgpack
map(2) {
  "visible": uint8(1),
  "text": str("提示或结果")
}
```

`visible` 支持 `uint8` 或 `bool`。显示时 `text` 必填且不能为空；隐藏时仅传 `visible=0` 即可。顶部文本使用不透明背景覆盖正文，不参与正文布局，因此显隐不会改变正文位置。顶部文本会保留到再次调用本命令、调用 `clearView` 或退出当前 Speech 页面。`setHeaderText` 与 `setLanguageHint` 互斥：显示顶部文本会隐藏语言提示，之后下发 `setLanguageHint` 会隐藏顶部文本并显示新的语言提示。

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
| `2` | 手表音源 |

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
| `setFunctionMenu` | 见 6.4 `setFunctionMenu` 字段 | `{}` |
| `setFontConfig` | 见 [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | 见 [6.1 文本记录](#61-文本记录) | `{}` |
| `setState` | `{ "state": uint8 }`，只支持 `0`/`1` | `{}` |
| `setHeaderText` | `{ "visible": uint8/bool, "text"?: string }` | `{}` |
| `setTextMode` | `{ "textMode": uint8 }` | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` 或 `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setTransMode` | `{ "transMode": uint8 }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |
| `setLanguageHint` | 见 [6.2 setLanguageHint](#setlanguagehint) | `{}` |
| `textDirection` | 见 [6.2 textDirection](#textdirection) | `{}` |

### 6.4 Translate

所有 Speech 入口的 `setLanguageHint` 均支持 `mode=0` 单语言提示和 `mode=1` 双语言提示，并复用同一套 Speech 页面实现。配置 `reportdoubleclick=true` 时，双击通过 `SystemInd.onTouchEvent` 上报 `event=2`，眼镜端不执行退出；需要确认时，可由手机通过现有 `TapMsgbox.showTapMsgbox` 协议显示提示框。配置 `reportdoubleclick=false` 时由眼镜端处理双击，`exitdoubleclick=true` 显示本地退出确认框，`exitdoubleclick=false` 直接退出。

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setFunctionMenu` | 见本节 `setFunctionMenu` 字段 | `{}` |
| `setFontConfig` | 见 [6.2 setFontConfig](#setfontconfig) | `{}` |
| `updateSttInfo` | 见 [6.1 文本记录](#61-文本记录) | `{}` |
| `setState` | `{ "state": uint8 }`，只支持 `0`/`1` | `{}` |
| `setHeaderText` | `{ "visible": uint8/bool, "text"?: string }` | `{}` |
| `setTextMode` | `{ "textMode": uint8 }` | `{}` |
| `setAudioTrackState` | `{ "audioTrack": uint8 }` 或 `{ "data": { "audioTrack": uint8 } }` | `{}` |
| `setTransMode` | `{ "transMode": uint8 }` | `{}` |
| `setMaxLine` | `{ "maxLine": uint32 }` | `{}` |
| `setAudioSourceIndicator` | `{ "audioSourceIndicator": uint8 }` | `{}` |
| `setMicDirectional` | `{ "micDirectional": uint8 }` | `{}` |
| `setLanguageHint` | 见 [6.2 setLanguageHint](#setlanguagehint) | `{}` |
| `textDirection` | 见 [6.2 textDirection](#textdirection) | `{}` |

`setFunctionMenu` 用于在 Speech 页面显示由手机控制的功能选择遮罩：

```msgpack
map(3) {
  "menuId": uint32(1),
  "selectedItemId": uint32(100),
  "items": array(2) [
    { "id": uint32(100), "label": str("模式一") },
    { "id": uint32(101), "label": str("模式二") }
  ]
}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `menuId` | `uint32` | 是 | 手机侧定义的菜单实例或版本 ID |
| `selectedItemId` | `uint32` | 是 | 当前高亮项 ID，必须匹配 `items[].id` |
| `items` | `array` | 是 | 至少包含 1 项，ID 不可重复；实际数量受单包大小和设备内存限制 |
| `items[].id` | `uint32` | 是 | 手机侧定义的选项 ID |
| `items[].label` | `string` | 是 | 眼镜端直接显示的非空选项文案，最长 63 个 UTF-8 字节 |

菜单显示期间，眼镜端不自行更改高亮项，也不发送专用的菜单选择消息。左滑、右滑、单击继续通过现有 `SystemInd.onTouchEvent` 上报，分别为 `event=6`、`event=7`、`event=1`；手机根据事件决定上一项、下一项或确认，并可重新下发 `setFunctionMenu` 刷新高亮项。

Roller 复用“上一项、当前项、下一项”3 个显示槽位循环展示。收到任意其他 Speech 命令时会隐藏该菜单；收到 `SystemControl.setProgressVisible` 且 `visible=1` 时也会隐藏。后续由手机下发对应 Speech 协议更新页面。

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

Prompter 使用 `id=5`，按 `cmd` 路由。配置 `reportdoubleclick=true` 时，双击通过 `SystemInd.onTouchEvent` 上报 `event=2`；配置为 `false` 时由眼镜端处理并直接退出提词器。

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `getTextLayout` | `{}` | `{ "breakAll": bool, "letterSpacePx": uint, "lineSpacePx": uint, "paddingHorizontalPx": uint, "paddingVerticalPx": uint, "textSizePx": uint, "totalHeightPx": uint, "totalWidthPx": uint }` |
| `setFontConfig` | 见 [6.2 setFontConfig](#setfontconfig) | `{}` |
| `setFileListMenu` | 见本节 `setFileListMenu` 字段 | `{}` |
| `setPrompterFile` | `{ "dir": string, "name": string, "size": uint32, "crc32": uint32 }` | `{}` |
| `seekTo` | `{ "offset": uint32, "length": uint32, "topMaskHeight": uint32, "bottomMaskHeight": uint32, "duration"?: uint32 }` | `{}` |
| `setTick` | `{ "tick": uint32 }` | `{}` |
| `setState` | `{ "state": uint32 }`，`0` 暂停，`1` 运行 | `{}` |

Prompter 支持可选的 `seekTo.duration`，单位毫秒。省略或传 `0` 时不播放滚动动画，直接跳转到目标文本位置；传入大于 `0` 的值时，相邻且可连续拼接的文本窗口按指定时长滚动，无法连续拼接时仍直接跳转。

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

## 8. Recorder

Recorder 使用 `id=1002`、`biz=recorder`，按 `cmd` 路由。命令仅在 Recorder 为当前页面时处理，否则返回 `ErrNotReady`。

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `setState` | `{ "state": uint32 }`，`0` 暂停并显示三角，`1` 运行、显示圆点并将时间重置为 `00:00:00` | `{}` |
| `setTick` | `{ "tick": uint32 }`，单位为秒 | `{}` |

`setTick` 将时间格式化为 `HH:MM:SS`；Running 状态下偶数 tick 显示圆点，奇数 tick 隐藏圆点。Pause 状态下始终显示三角。

## 9. Gallery

Gallery 使用 `id=7`，按 `cmd` 路由。

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}` |
| `setGalleryFile` | `{ "dir": string, "name": string }` | `{}` |

## 10. Navigation

Navigation 使用 `id=4`、`biz=navigation`。应先通过 `SystemControl.setView` 进入 `navigation` 页面。

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `clearView` | `{}` | `{}`；清空导航数据并显示起始提示 |
| `updateNav` | 导航对象，见下表 | `{}` |
| `updateBpm` | `{ "bpm": string }` | `{}`；空字符串隐藏心率区域 |
| `updateSpo` | `{ "spo": string }` | `{}`；空字符串隐藏血氧区域 |

`updateNav` 字段：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `navMode` | `int32` | 否 | `1=驾车`、`2=步行`、`3=骑行`；其它值使用驾车图标 |
| `nextRoadName` | `string` | 否 | 下一道路名称 |
| `curStepRetainDistance` | `string` | 否 | 当前步骤剩余距离 |
| `remainDistance` | `string` | 否 | 全程剩余距离 |
| `remainTime` | `string` | 否 | 全程剩余时间 |
| `speed` | `string` | 否 | 当前速度；空字符串隐藏速度区域 |
| `iconBytes` | `bin(2304)` | 否 | 48×48 L8 方向图；未提供时沿用当前方向图，长度不匹配时忽略该图标 |

## 11. ImageFusion

ImageFusion 使用 `id=1001`、`biz=imagefusion`、`viewName=imagefusion`，用于读取或设置双目显示融合偏移。

| cmd | 请求 `data` | 成功 ACK `data` |
| --- | --- | --- |
| `getFusionParams` | `{}` | `{ "lX": int8, "lY": int8, "rX": int8, "rY": int8 }` |
| `setFusionParams` | `{ "lX": int32, "lY": int32, "rX": int32, "rY": int32 }` | `nil` |

四个偏移字段均必填，取值范围为 `-128..127`。参数缺失、类型错误或越界返回 `ErrBadParam`；底层读写失败返回 `ErrDataErr`。

## 12. 维护规则

新增或修改协议时，按以下顺序检查：

1. `common/message.h` 中的 `id/type/error` 是否需要更新。
2. 目标模块是否已注册 `app_message_t`。
3. 目标命令是否在对应 `app_cmd_func_t` 表中。
4. 请求方向的 `payload.data` 是否保持 map 或 nil。
5. 成功路径和失败路径是否都返回明确 ACK/NACK。
6. 主动上报是否使用 `system_report_next_sequence()` 或模块对应的 sequence 生成方式。
7. 文档是否同步更新。
