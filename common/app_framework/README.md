# App Framework

轻量 App/Page 框架与固定 UI 层级，用于承载业务 App 页面栈、系统壳层、弹窗浮层和双眼显示偏移。

当前目录包含：

- `app_layers` / `app_stereo`：已接入运行时，用于固定层级、显示距离同步和双眼业务 framebuffer 输出。
- `app_router`：全局 App 路由入口，负责注册 App、解析首页、切换 App 和维护进入方式。
- `app_manager` / `app_nav` / `app_page_host`：App 页面栈接口，所有业务 App 统一通过这里注册、切换和管理生命周期。

## 文件职责

| 文件 | 职责 |
| --- | --- |
| `app_layers.[ch]` | 创建并维护业务固定层级，提供 `app_layers_get_*()` 给页面、弹窗和系统壳层挂载对象。 |
| `app_stereo.[ch]` | 管理双眼业务输出尺寸、眼区起点、浮层视差和右眼 framebuffer 渲染/复制。 |
| `app_router.[ch]` | 全局 App 路由入口，负责 App 注册、首页解析、切换策略、进入方式和 view change 上报。 |
| `app_page_host.[ch]` | 为新页面栈创建 `scene_root` 与 `content_root`，转场动画应作用在 `scene_root`，业务内容挂到 `content_root`。 |
| `app_manager.[ch]` | 注册、切换、停止 App，并维护每个 App 的独立页面栈和生命周期。 |
| `app_nav.[ch]` | App 内页面导航门面，当前薄转发到 `app_page_*()`，用于让业务代码少依赖 manager 细节。 |

## 固定层级

`system_init_lvgl_fb()` 初始化 LVGL 根节点后调用 `app_layers_init()`。框架在活动屏幕下创建统一业务根层，内部层级如下：

```text
lv_screen_active()
└── root
    ├── background
    ├── app
    │   ├── page host
    │   └── bottom status bar
    ├── popup
    ├── overlay
    └── top
```

- `root`：业务层级根，承载 app framework 的所有固定层。
- `background`：背景层。
- `app`：页面内容和底部状态栏，保持在业务坐标系内。
- `popup`：toast、msgbox、notify、assistant 等普通浮层，使用浮层距离偏移。
- `overlay`：蓝牙断连、锁屏等系统遮罩，压过普通 popup。
- `top`：最高优先级 UI，例如调试 HUD；普通弹窗不要放这里。

`app_layers_resize()` 会同时刷新层尺寸和左眼场景位置。显示距离档位变化后，系统壳层将根图像距离下发到底层，再调用 `app_layers_resize()` 同步页面、状态栏和浮层位移。

## 双眼输出

`app_stereo` 统一描述业务 UI 区域与双眼业务输出区域：

- 逻辑 UI 尺寸来自 `SYSTEM_LCD_UI_WIDTH` / `SYSTEM_LCD_UI_HEIGHT`，当前单眼为 `540x440`。
- 单眼业务区域来自 `SYSTEM_LCD_EYE_FRAME_WIDTH` / `SYSTEM_LCD_EYE_FRAME_HEIGHT`。
- 双眼竖向业务输出为 `540x880`；app 层不再关心底层物理屏幕的 `640x960` 映射。
- 输出 framebuffer 尺寸由 `app_stereo_get_output_width()` 和 `app_stereo_get_output_height()` 计算。
- 眼区起点由 `app_stereo_get_eye_origin()` 返回，默认 ARM 侧为上下竖向堆叠。
- 根层 3D 距离只通过 `jyt_dual_screen_set_root_distance()` 下发，root x/y 业务坐标不再调用旧 OS 转换接口。
- 浮层节点横向距离由 app framework 按眼位应用固定视差，左眼减距离、右眼加距离。

输出模式：

| 模式 | 说明 |
| --- | --- |
| `APP_STEREO_OUTPUT_VERTICAL` | 双眼上下堆叠；ARM 默认且当前唯一支持模式。 |
| `APP_STEREO_OUTPUT_HORIZONTAL` | 双眼左右并排；仅 native/simulator 侧可在安装 display mirror 前设置。 |
| `APP_STEREO_OUTPUT_SINGLE` | 仅输出单眼业务区域；仅 native/simulator 侧用于调试。 |

初始化顺序约定：

1. `system_init_lvgl_fb()` 根据 `app_stereo_get_output_width()` / `app_stereo_get_output_height()` 设置 LVGL 根对象。
2. `app_layers_init()` 注册右眼分层渲染回调。
3. display mirror 安装后，`app_stereo` 在 `LV_EVENT_FLUSH_START` 阶段生成右眼画面。

右眼渲染优先使用 `app_layers` 的分层重绘回调，分别重绘 `background`、`app`、`popup`、`overlay`、`top` 以及 LVGL 原生 top/sys layer；如果回调失败，则回退到把左眼业务区域复制到右眼业务区域。

## App 与 Page 生命周期

App 负责应用级生命周期：

```text
on_start   首次启动；通常在这里压入根页面
on_resume  从后台恢复
on_pause   切走到后台
on_stop    停止并清空页面栈
on_back    根页面返回处理
```

Page 负责页面级生命周期：

```text
on_create     创建 LVGL 对象，parent 为 content_root
on_appear     页面视图已创建并可见
on_disappear  页面即将不可见
on_destroy    页面视图即将销毁，清理页面保存的 LVGL 指针
on_unload     页面从栈移除，释放非 LVGL 资源
on_back       页面自定义返回；返回 true 表示已消费
```

页面跳转顺序：

- `app_nav_push()` / `app_page_push()`：销毁当前栈顶视图，复制页面入参，压入新页面并创建新视图。
- `app_nav_pop()` / `app_page_pop()`：仅允许弹出非根页面；销毁当前视图并触发 `on_unload()`，再恢复上一页视图。
- `app_nav_replace()` / `app_page_replace()`：替换当前栈顶页面；若当前栈为空，则创建第一个页面。
- `app_nav_back()` / `app_page_back()`：先交给页面 `on_back()`，未消费且有上一页时执行 pop，根页面再交给 App `on_back()`。

页面入参会按 `size` 复制到栈项中，页面只应在自身生命周期内读取 `app_page_data_t` 中的数据，不要保存其中的裸指针作为长期所有权。

## App 路由

`app_router.c` 是全局 App 路由入口。`app_router_set_app()` / `app_router_call_home()` 调用 `app_manager` 切换 App，并集中处理以下策略：

- 切换前检查 `app_manager` 是否 busy。
- 当前有来电 notify 或蓝牙断连遮罩时，按规则阻止切换。
- 切换时清理底部状态栏自定义 widget。
- 本地进入时上报 view change，远端拉起时抑制重复上报。
- 根据语言选择、新手引导和配置首页解析 `app_router_call_home()` 目标。

新增 App 时通常需要：

1. 在 `cmake/CommonSources.cmake` 的 app 列表中登记构建宏来源，并确认未被 `build_apps_denylist.txt` 屏蔽；`app_build_config.h` 是生成文件，不要手动修改。
2. 在 `app_router.c` 引入 app 头文件并注册 `*_app_register()`。
3. 确认 `*_app_register()` 已在 `app_router_init()` 注册。
4. 检查首页配置、远端 `setView` 名称和退出返回首页行为。

## 使用边界

- 框架层只暴露 `lv_obj_t*`，不依赖 `container.h`。
- 页面内部可以使用 `container`、`label`、`img` 等现有 widget。
- 底部状态栏由 `system_runtime_ui` 在 `app` 层统一创建和显隐，页面不要创建私有底部状态栏。
- 系统级遮罩挂到 `overlay`，普通通知和弹窗挂到 `popup`。
- 新增或维护 App 时优先通过 `app_nav_*()` 做页面跳转，避免业务代码直接操作页面栈结构。
- 修改显示距离、双眼输出或层级顺序时，要同步检查 `system_runtime_ui.c`、弹窗 widget 和 simulator/native 输出模式。

## 新增 App 建议

新增业务 App 时，优先按现有 App 的结构拆分：

1. `app.c` 负责 App 生命周期、消息注册和根页面压栈。
2. `view.c` 持有页面描述符 `app_page_t` 和页面生命周期函数。
3. 页面切换通过 `app_nav_*()` 完成。
4. 全局切 App 统一从 `app_router` 进入。
