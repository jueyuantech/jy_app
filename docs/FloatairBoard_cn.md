# FloatairBoard 环境准备

英文版：[FloatairBoard.md](FloatairBoard.md)

`FloatairBoard` 文档用于说明眼镜端真实板卡的开发环境、ARM 构建输入、资源准备和构建产物。桌面模拟器环境请看 [FloatairSimulator_cn.md](FloatairSimulator_cn.md)。

## 1. 适用范围

本文覆盖 `jy_app` 的 ARM 固件 app 层目标：

- 目标平台：`arm`
- 平台适配目录：`bes28/`
- 构建目标名：`uimain`
- 构建入口：仓库根目录 `CMakeLists.txt`
- 文件系统打包脚本：`scripts/fs_img.py`

Host 协议字段请看 [datapath_v3_protocol_cn.md](datapath_v3_protocol_cn.md)。

## 2. 基础工具

| 工具 | 要求 | 说明 |
| --- | --- | --- |
| CMake | 3.10 或以上 | 顶层 `CMakeLists.txt` 的最低版本要求 |
| Python 3 | 可执行解释器 | CMake 会查找 `Python3::Interpreter`，用于产品 overlay、UI 资源生成和文件系统打包 |
| ARM GCC | `arm-none-eabi-gcc` | ARM 构建只支持 GCC，不支持 Clang |
| ARM binutils | `arm-none-eabi-nm` 等 | `scripts/fs_img.py` 会用 `arm-none-eabi-nm` 做符号检查 |
| Python littlefs 包 | `littlefs` | `scripts/fs_img.py` 使用 `from littlefs import LittleFS` 生成 LittleFS 镜像 |
| genromfs | 可选 | 存在时用于生成 ROMFS；不存在时脚本会使用内置 Python ROMFS writer |
| 7-Zip | `7z` / `7za` | 传入 `JY_APP_OS_SDK_ARCHIVE` 时，CMake 用它解压 OS SDK 包 |

`arm-none-eabi-gcc` 可以放入 `PATH`，也可以在 CMake 配置时通过 `-DCMAKE_C_COMPILER=<path-to-arm-none-eabi-gcc>` 显式指定。

## 3. 工程配置

### 3.1 `.config`

ARM 平台配置会读取仓库根目录 `.config`，并把其中的配置项转换成编译宏。`.config` 缺失时，ARM 配置会失败。

修改 Kconfig 配置后，需要重新执行完整 CMake 配置流程，不要只增量编译。

### 3.2 目标平台识别

`cmake/TargetPlatform.cmake` 根据编译器识别目标平台：

- `arm-none-eabi-gcc` -> `arm`
- 未显式指定编译器时，如果 `PATH` 中存在 `arm-none-eabi-gcc`，默认选择 `arm`
- 如果要构建桌面模拟器，需要显式指定对应平台编译器，详见 [FloatairSimulator_cn.md](FloatairSimulator_cn.md)

### 3.3 产品 overlay

`JY_APP_PRODUCT` 用于选择产品 overlay，默认值为 `jytek`。CMake 配置阶段会先清理 overlay，再应用选中的产品 overlay。

`JY_APP_PRODUCT=clean` 只用于清理 overlay，不能直接产出构建。

### 3.4 OS SDK 包

`jy_app` 不再在 app 仓内维护从 OS 仓同步来的 `floatair`、`nuttx`、`vendor` 头文件。ARM 和模拟器构建都会使用 OS 仓导出的 OS SDK 包。

首次配置时传入 OS SDK 包：

```bash
cmake -S . -B build \
  -DCMAKE_C_COMPILER=arm-none-eabi-gcc \
  -DJY_APP_OS_SDK_ARCHIVE=/path/to/jy_os_sdk_<branch>_<tag>_<count>_g<hash>_dev.7z
```

CMake 会计算 7z 包 SHA256，解压到 `.os_sdk_cache/<short-sha256>/os_sdk/`，并在缓存目录旁记录完整 SHA256。后续配置可以不再传 `JY_APP_OS_SDK_ARCHIVE`，CMake 会复用最新的有效缓存；如果既没传包，也没有有效缓存，配置会直接失败。

SDK 包包含：

| SDK 目录 | 用途 |
| --- | --- |
| `os_sdk/floatair/` | ARM 和模拟器共用的 Floatair 头文件 |
| `os_sdk/nuttx/` | ARM include 路径需要的 NuttX 头文件 |
| `os_sdk/vendor/` | ARM include 路径需要的 vendor 头文件 |
| `os_sdk/manifest.json` | OS 仓打包脚本生成的包清单 |

`cmake/OsSdk.cmake` 还会在构建目录生成 `os_sdk_overrides/` 头文件，用于注入 app 日志前缀 `FLA>`，不会修改解压后的 SDK 缓存。

## 4. 资源准备

| 目录 / 文件 | 用途 |
| --- | --- |
| `romfs/` | 固件只读资源，ARM 构建时会复制到构建目录 staging 后打包 |
| `lfsc/` | LittleFS C 分区源目录，构建产物 `uimain` 会复制到这里参与打包 |
| `lfsd/` | LittleFS D 分区源目录，会生成 `nuttx_lfsd.bin` |
| `StringPool.csv` | 多语言字符串池输入，打包时生成 i18n JSON |
| `ui.res.json` | UI 资源描述输入 |
| `apps/**/*.ui.json` / `system/**/*.ui.json` | UI 编译器输入，生成到构建目录 `generated/ui` |

## 5. 启动 Logo

引导启动 Logo 使用静态图片：

| 项目 | 要求 |
| --- | --- |
| 路径和文件名 | `/jyt_d/apps/logo/logo.png` |
| 图片格式 | 推荐 PNG，支持 PNG / JPG / BMP |
| 推荐尺寸 | `240x80` |
| 最大尺寸 | 不能超过光机显示尺寸 `640x480` |
| 背景 | 透明或纯黑色 |
| 图形元素 | 纯白色或纯绿色 |

## 6. 构建流程

ARM 构建的核心流程：

1. CMake 识别 `arm` 平台并加载 `cmake/PlatformArm.cmake`。
2. 收集 `apps/`、`system/`、`common/`、`lvgl/`、`thirdparty/` 和生成 UI 源码。
3. 生成 `uimain` ELF。
4. 构建后复制 `romfs/` 到构建目录 staging。
5. 调用 `scripts/fs_img.py --source <uimain> --romfs-dir <romfs_staging>`。
6. 生成文件系统镜像。

常见 CMake 配置形式：

```bash
cmake -S . -B build \
  -DCMAKE_C_COMPILER=arm-none-eabi-gcc \
  -DJY_APP_OS_SDK_ARCHIVE=/path/to/jy_os_sdk_<branch>_<tag>_<count>_g<hash>_dev.7z
cmake --build build
```

如果当前 shell 的 `PATH` 已包含 `arm-none-eabi-gcc`，也可以不显式传 `CMAKE_C_COMPILER`。如果本地已经有有效 `.os_sdk_cache/`，也可以不再传 `JY_APP_OS_SDK_ARCHIVE`。

## 7. 构建产物

ARM 构建后，构建目录中会生成：

| 产物 | 说明 |
| --- | --- |
| `uimain` | ARM app 层 ELF |
| `nuttx_lfsc.bin` | LittleFS C 分区镜像 |
| `nuttx_lfsd.bin` | LittleFS D 分区镜像 |
| `nuttx_romfs.bin` | ROMFS 镜像 |
| `generated/` | UI 编译、资源头文件和构建配置等生成内容 |

## 8. 常见问题

| 现象 | 检查项 |
| --- | --- |
| CMake 提示找不到 ARM 编译器 | 确认 `arm-none-eabi-gcc` 在 `PATH` 中，或使用 `-DCMAKE_C_COMPILER` 指定完整路径 |
| CMake 提示 `.config` 缺失 | 确认仓库根目录存在 `.config` |
| CMake 提示缺少 OS SDK 缓存 | 先传一次 `-DJY_APP_OS_SDK_ARCHIVE=<path-to-jy_os_sdk_..._dev.7z>`，或保留一个有效 `.os_sdk_cache/` |
| CMake 找不到 7-Zip | 安装 7-Zip，或设置 `-DJY_APP_OS_SDK_SEVEN_ZIP=<path-to-7z>` |
| `fs_img.py` 导入 `littlefs` 失败 | 给当前 Python 环境安装 `littlefs` 包 |
| 符号检查失败 | 检查是否漏编源文件、配置宏是否正确、依赖符号是否属于允许范围 |
| ROMFS 生成失败 | 检查 `romfs/` 是否存在，内容大小是否超过分区限制 |
| LittleFS 生成失败 | 检查 `lfsc/`、`lfsd/` 内容大小是否超过分区容量 |

## 9. 与模拟器的区别

| 项目 | Board | Simulator |
| --- | --- | --- |
| 目标平台 | `arm` | `linux` / `macos` / `mingw` / `msvc` |
| 平台目录 | `bes28/` | `simulator/FloatairSimulator/` |
| 主要产物 | `uimain` 和文件系统镜像 | `floatair_simulator` / `floatair_simulator.exe` |
| 图形输出 | 真实光机 | SDL2 窗口 |
| 资源路径 | 打包进 LittleFS / ROMFS | 构建目录下 `jyt_d/` / `romfs/` |
