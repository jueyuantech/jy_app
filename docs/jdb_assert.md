# jy_app 崩溃验证

应用启动后，在右腿 OS shell 执行：

```sh
jdb --assert=elf
jdb --assert=elf,oom
```

两条命令分别测试：uimain 内真实断言；uimain 持续申请 256 KiB 内存块直至 malloc 失败。每条都会导致崩溃复位，应分次验证。malloc 失败优先走 OS 内存诊断与 panic；若 OS 未开启分配失败 panic，应用在返回 NULL 后触发断言。

OS 经 M55 向 Q-8 发送 `EMT_SYSTEM_EVENT_WITH_PAYLOAD`，事件号 **1010**（OS 名称 `SET_JYT_ASSERT_TRIG`，应用名称 `APP_SYSTEM_EVENT_JDB_ASSERT`），固定 1 字节 payload：0=assert，1=OOM。应用在页面拦截前处理；长度或模式无效时拒绝。1010 已占用，后续事件不得复用。

观察 dump 收尾的 `[VERSIONS] OS=... APP=...`，以及重启后按现有保存策略回捞的墓碑。`jdb --assert` 不带 `=elf` 仍触发 AP 断言。
