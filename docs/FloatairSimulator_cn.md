# FloatairSimulator

英文版：[FloatairSimulator.md](FloatairSimulator.md)

`FloatairSimulator` 是应用工程的桌面模拟器入口，当前支持 `linux`、`macos`、`mingw`、`msvc` 四种构建平台，对应产物统一为 `floatair_simulator`（Windows 下为 `floatair_simulator.exe`）。

这份 README 以仓库里的现有脚本和 CMake 逻辑为准，重点说明“怎么编、怎么跑、运行时依赖放在哪”。

## 目录概览

```text
FloatairSimulator/
├── README.md
├── main.c
├── sys_adapter.c
├── floatair_lcd.c
├── floatair_fs.c
├── lv_port_fs.c                 # LVGL 文件系统端口
├── sim_socket.c
├── simulator_event_fifo.c       # 系统事件 FIFO / Named pipe 输入
├── simulator_platform.c         # 平台适配分发
├── simulator_socket.conf        # 模拟器 TCP 目标地址配置
├── simulator_event_panel.py     # OS 事件注入面板
├── build_all.sh                 # Linux/macOS 下探测并构建所有可支持组合
├── build_all.bat                # Windows 下探测并构建所有可支持组合
├── develop-simulator.sh         # Linux/macOS/Windows MinGW 交叉编译统一入口
├── develop-simulator-mingw.bat  # Windows MinGW 入口，转发到 develop-simulator.ps1
├── develop-simulator-msvc.bat   # Windows MSVC 入口，转发到 develop-simulator.ps1
├── develop-simulator.ps1        # Windows MinGW/MSVC 共用构建逻辑
├── linux/
└── windows/
```

## 当前运行逻辑

模拟器运行时依赖两类外部内容：

1. `jyt_d/`
   CMake 在构建完成后会把仓库根目录下的 `lfsd/` 复制到输出目录旁，生成运行时资源目录 `jyt_d/`。
   模拟器里的 `/jyt_d/...` 路径会映射到“可执行文件所在目录”下的 `jyt_d/`。

2. `simulator_socket.conf`
   文件内容为三行：
   - 第 1 行：主机地址，默认 `127.0.0.1`
   - 第 2 行：端口，默认 `24680`
   - 第 3 行：显示展开方式，`0` 为左右并排，`1` 为上下堆叠，`2` 为单眼显示

   CMake 会把源目录下的 `simulator_socket.conf` 复制到构建输出目录。
   模拟器读取配置时会优先查找“可执行文件所在目录”，找不到时再回退到当前启动工作目录。因此最稳妥的启动方式是使用仓库内提供的脚本，或者先 `cd` 到对应 `build-*` 目录再运行模拟器。

   源目录当前默认配置是：

   ```text
   127.0.0.1
   24680
   2
   ```

   如果第 3 行缺失或无法识别，代码会回退到 `1`，即上下堆叠双眼输出。

模拟器默认会持续重连 `127.0.0.1:24680`，用于和手机侧测试服务联调；如需连接其他地址或端口，可以修改 `simulator_socket.conf`。

## 安卓模拟器联调

Windows 桌面模拟器和安卓模拟器联调时，推荐先让雷电、蓝叠等安卓模拟器里的手机侧测试服务监听 `24680`，再使用 `forward-jyapp-simulator.ps1` 把电脑本机的 `127.0.0.1:24680` 转发到选中的安卓模拟器设备。这样 `floatair_simulator` 保持默认 `simulator_socket.conf` 即可连接到安卓侧服务。

### 雷电 / 蓝叠准备

如果系统里还没有可用的 `adb.exe`，可以下载 Google 官方 Android SDK Platform-Tools：

- 官方下载页：`https://developer.android.com/tools/releases/platform-tools`
- Windows 直链：`https://dl.google.com/android/repository/platform-tools-latest-windows.zip`

解压后把 `platform-tools` 目录加入 `PATH`，或者运行脚本时通过 `-Adb <ADB_EXE_PATH>` 指定 `adb.exe` 的完整路径。

先启动安卓模拟器，再确认 `adb devices` 能看到设备：

```bat
adb devices
```

常见设备显示可能是 `emulator-5554`、`127.0.0.1:5555`、`localhost:5555` 或模拟器厂商自己的设备名，最终以 `adb devices` 实际输出为准。

如果 `adb devices` 没有列出雷电或蓝叠设备，可以先用模拟器的 ADB 地址连接。不同版本和多开实例端口可能不同，优先看模拟器设置页或多开器里的 ADB 端口；常见形式如下：

```bat
adb connect 127.0.0.1:5555
adb devices
```

蓝叠如果没有使用系统 `adb`，也可以先确认 BlueStacks 自带 ADB 是否可用，再通过脚本的 `-Adb` 参数指定它，例如：

```bat
"C:\Program Files\BlueStacks_nxt\HD-Adb.exe" devices
```

### forward-jyapp-simulator.ps1

`forward-jyapp-simulator.ps1` 的作用是：

1. 可选执行 `adb connect <地址>`
2. 读取当前在线的 ADB 设备
3. 未指定设备时让用户选择一个设备
4. 移除该设备上已有的本地端口转发
5. 建立 `local tcp:<Port> -> device tcp:<Port>` 转发
6. 打印当前设备的 `adb forward --list`

脚本参数如下：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-Device` | 空 | 指定 `adb devices` 里显示的设备名；为空时脚本会列出设备并让用户选择 |
| `-Port` | `24680` | 本机和安卓设备侧使用的 TCP 端口 |
| `-Adb` | `adb` | ADB 可执行文件；可指定完整路径，例如蓝叠的 `HD-Adb.exe` |
| `-Connect` | 空 | 脚本开始时先执行 `adb connect <地址>`，适合雷电/蓝叠没有自动出现在设备列表时使用 |

常用命令：

```powershell
# 进入模拟器目录
cd jy_app\simulator\FloatairSimulator

# 自动列出 ADB 设备并选择，默认转发 24680
.\forward-jyapp-simulator.ps1

# 指定设备名
.\forward-jyapp-simulator.ps1 -Device emulator-5554

# 先连接雷电 / 蓝叠 ADB 地址，再选择设备
.\forward-jyapp-simulator.ps1 -Connect 127.0.0.1:5555

# 指定蓝叠自带 ADB
.\forward-jyapp-simulator.ps1 -Adb "C:\Program Files\BlueStacks_nxt\HD-Adb.exe" -Connect 127.0.0.1:5555

# 如果安卓侧服务改了端口，桌面模拟器的 simulator_socket.conf 也要同步改成同一个端口
.\forward-jyapp-simulator.ps1 -Port 24680
```

脚本成功后会看到类似输出：

```text
Using adb device: emulator-5554
Forwarding local tcp:24680 -> device tcp:24680

Current forward list:
emulator-5554 tcp:24680 tcp:24680
```

### 联调启动顺序

推荐顺序如下：

1. 启动雷电 / 蓝叠
2. 启动安卓侧 App 或测试服务，确保设备侧 `24680` 已监听
3. 执行 `forward-jyapp-simulator.ps1`
4. 启动 `floatair_simulator`

如果桌面模拟器已经启动，也可以直接重新执行 `forward-jyapp-simulator.ps1`；模拟器默认会持续重连 `127.0.0.1:24680`。

常见排查：

- `No adb device found`：先启动安卓模拟器，再执行 `adb devices`；必要时使用 `-Connect 127.0.0.1:<端口>`
- 设备列表里有多个设备：用 `-Device <设备名>` 固定目标，避免选错实例
- 连接后仍不通：确认安卓侧服务监听端口和 `simulator_socket.conf` 第 2 行一致，默认都是 `24680`
- 蓝叠找不到设备：尝试使用 `-Adb "C:\Program Files\BlueStacks_nxt\HD-Adb.exe"` 指定蓝叠自带 ADB

## 平台与前置要求

推荐优先使用 32 位模拟器环境，和真机 ARMv7 的指针宽度与数据布局更接近；64 位环境主要用于 UI 展示、交互预览和日常调试，不建议作为唯一验证路径。

当前编译器支持边界如下：

| 平台 | 支持编译器 | 说明 |
| --- | --- | --- |
| `linux` | `gcc` / `clang` | 本地 Linux 模拟器，默认按 32 位构建，可通过 `ARCH` 切换 |
| `macos` | AppleClang / `clang` | 本地 macOS 模拟器，仅支持 64 位 |
| `mingw` | MinGW `gcc` / MinGW `clang` | Windows MinGW 构建，支持 Windows 本机构建和 Linux/macOS 交叉编译，支持 x86 / x64，链接 MinGW ABI 的 `.dll.a` |
| `msvc` | `cl` / Visual Studio `clang` / `clang-cl` | 支持 x86 / x64，链接 MSVC ABI 的 `.lib` |

Windows 侧已验证的组合是：MinGW `gcc` / `clang`，MSVC `cl` / Visual Studio `clang` / `clang-cl`，x86 / x64 均可构建。
Linux/macOS 侧可以通过 MinGW `gcc` 或 LLVM/Clang 交叉编译 Windows MinGW 产物；CMake 会继续按传入的编译器名称或 `CMAKE_C_COMPILER_TARGET` 推断平台，不需要额外传入 toolchain file。

Windows 下需要区分“命令行前端”和“目标 ABI”：

- Visual Studio LLVM 的 `clang.exe` 使用 GNU-like 参数，例如 `-I`、`-D`、`-W...`，但它链接 `lld-link.exe` 和 MSVC `.lib`，因此归到 `msvc` 平台。
- Windows 宿主或显式 `windows-msvc` target 下的 `clang-cl.exe` 使用 MSVC-like 参数，例如 `/I`、`/D`、`/wd...`，也归到 `msvc` 平台。
- MinGW / LLVM-MinGW 的 `clang.exe` 使用 MinGW ABI，归到 `mingw` 平台。
- Linux/macOS 上的 MinGW 工具链会生成 Windows GNU 目标产物；项目会根据 `i686-w64-mingw32-gcc`、`x86_64-w64-mingw32-gcc` 或带 `windows-gnu` target 的 Clang 自动归到 `mingw` 平台。
- macOS 的 LLVM 包可能也提供 `clang-cl`，但它仍是 MSVC 风格前端，会把 `/Users/...` 路径解析成 MSVC 参数，不能用于本地 macOS 模拟器。

### 批量构建

Linux/macOS 下可以使用统一入口探测当前环境，并构建所有可支持的组合：

```bash
./simulator/FloatairSimulator/build_all.sh
```

Linux 主机下脚本会检查这些能力：

- 本地 `gcc`、`clang`
- MinGW `gcc`
- MinGW `clang`，包括带目标三元组前缀的 `*-clang` 或可用的 `clang --target=<mingw-triple>`
- Linux SDL2 x86 和 x64 开发环境

macOS 主机下脚本会检查这些能力：

- 本地 `clang`
- macOS SDL2 开发环境
- MinGW `gcc`
- MinGW `clang`，包括带目标三元组前缀的 `*-clang`

只构建检查通过的组合；缺少依赖的组合会跳过并打印原因。本地 Linux/macOS 构建会自动传入 `--no_run`，因此只构建并安装产物，不启动模拟器。每个组合会安装到项目根目录下对应的 `install/linux-*`、`install/mingw-*` 或 `install/macos-*` 目录。

如果只想查看当前环境会构建哪些组合：

```bash
./simulator/FloatairSimulator/build_all.sh --dry-run
```

Windows 下对应入口是：

```bat
simulator\FloatairSimulator\build_all.bat
```

交互运行这些脚本时，选择产品后会提示输入 OS SDK 包路径；直接回车表示使用最新 `.os_sdk_cache/` 缓存。命令行也可以把 7z 包路径作为第一个位置参数传入，例如 `develop-simulator.sh /home/user/os_sdk.7z` 或 `develop-simulator-mingw.bat C:\path\os_sdk.7z`。

如果不希望脚本结束后保留窗口，可以传入 `--no-pause`；`build_all.bat` 调用 `develop-simulator.ps1` 时也会自动传入 `--no-pause`。

Windows 脚本会检查这些能力：

- MinGW `gcc`：x86 查找 `i686-w64-mingw32-gcc.exe`，x64 查找 `x86_64-w64-mingw32-gcc.exe`
- MinGW `clang`：x86 查找 `i686-w64-mingw32-clang.exe`，x64 查找 `x86_64-w64-mingw32-clang.exe`
- MSVC `cl`
- Visual Studio LLVM `clang`
- Visual Studio LLVM `clang-cl`

MinGW `gcc` 优先从 `MINGW` 环境变量列出的目录查找，MinGW `clang` 优先从 `LLVM` 环境变量列出的目录查找；找不到时再 fallback 到 `PATH` 和单目录兜底变量。只构建检查通过的组合；缺少依赖的组合会跳过并打印原因。批量构建会自动传入 `--no_run`，因此只构建并安装产物，不启动模拟器。每个组合会安装到项目根目录下对应的 `install\mingw-*` 或 `install\msvc-*` 目录。

如果只想查看当前 Windows 环境会构建哪些组合：

```bat
simulator\FloatairSimulator\build_all.bat --dry-run
```

### Ubuntu / Linux

- 构建入口：`develop-simulator.sh`；Linux 宿主机默认平台就是 `linux`
- 默认构建目录：`simulator/FloatairSimulator/build-linux-x86-gcc`
- 默认安装目录：`install/linux-x86-gcc`
- 平台选择：脚本会显式传入本地 `gcc`，CMake 再根据传入工具链识别为 `linux`
- 默认 `ARCH=x86`，会使用 `-m32` 编译，因此需要可用的 32 位编译/链接环境
- 如需 64 位本机构建，脚本可切换到 x64 目标；手动配置时仍需传给 CMake `-DARCH=amd64`
- Linux 下 `x86` 使用 `pkg-config` 查找 i386 SDL2，默认路径为 `/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig`；如果没有 i386 `sdl2.pc`，`build_all.sh` 会跳过该组合
- 脚本 `x64` 对应 CMake `amd64`，使用 `find_package(SDL2)` 查找系统 SDL2
- 推荐使用 Ubuntu 20.04 作为 Linux 模拟器开发环境，x86 和 x64 本机构建依赖都兼容
- 在非推荐环境中，不推荐为了本机 Linux x86 构建强行安装 32 位 SDL2；`libsdl2-dev:i386` 可能和当前桌面/系统包冲突，无法安全安装时让 `build_all.sh` 跳过该组合，或改用容器/虚拟机环境
- 依赖：
  - `cmake`
  - `ninja`
  - `gcc`
  - `SDL2` 开发库
  - `pthread`

Linux 依赖安装以 Ubuntu 20.04 / Debian 系为准，其他发行版未验证，可按各自包管理器自行适配。

如果只需要 x64 本机构建，安装基础工具和 SDL2 即可：

```bash
sudo apt update
sudo apt install cmake ninja-build gcc clang libsdl2-dev
```

如果需要默认的 x86 构建，需要额外启用 i386 架构并安装 32 位编译/链接环境和 i386 SDL2 开发包。Ubuntu 20.04 下 x86 和 x64 依赖兼容；其他环境安装 `libsdl2-dev:i386` 前必须仔细检查 apt 计划。如果会卸载 `ubuntu-desktop-minimal`、`gnome-shell`、`network-manager`、`python3` 等核心包，或因为 SDL2 32 位包与系统包冲突无法安装，就跳过本机 Linux x86 构建或换容器/虚拟机环境：

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install gcc-multilib g++-multilib libc6-dev-i386 libsdl2-dev:i386
```

如果希望 `build_all.sh` 同时探测本地 `gcc` / `clang` 和 MinGW 交叉构建，可以再安装 MinGW 工具链：

```bash
sudo apt install mingw-w64
```

如果 x86 SDL2 的 `pkgconfig` 文件安装在非默认位置，可以在配置时覆盖：

```bash
cmake -S . -B simulator/FloatairSimulator/build-linux-x86-gcc -G Ninja \
  -DCMAKE_C_COMPILER=gcc \
  -DARCH=x86 \
  -DLINUX_X86_SDL2_PATH="<SDL2_I386_PKGCONFIG_DIR>:/usr/share/pkgconfig"
```

Linux 构建脚本也支持显式选择编译器和架构；不传参数时保持默认 `gcc + x86 + build-linux-x86-gcc`，并自动安装到项目根目录下的 `install/linux-x86-gcc`。
如果只想构建并安装、不启动模拟器，可以额外传入 `--no_run`。
如果需要指定安装目录，可以额外传入 `--prefix <INSTALL_DIR>`。
如果只想检查当前 Linux 构建依赖，可以传入 `--check-platform-deps`。

```bash
./simulator/FloatairSimulator/develop-simulator.sh
./simulator/FloatairSimulator/develop-simulator.sh --compiler llvm
./simulator/FloatairSimulator/develop-simulator.sh --check-platform-deps
./simulator/FloatairSimulator/develop-simulator.sh --arch x86 --no_run
./simulator/FloatairSimulator/develop-simulator.sh --arch x86 --prefix install/linux-x86-gcc --no_run
```

手动配置 LLVM/Clang 示例：

```bash
cmake -S . -B simulator/FloatairSimulator/build-linux-x86-llvm -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DARCH=x86 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-linux-x86-llvm
```

#### Ubuntu / Linux 排错要点

- 默认脚本走 x86；如果没有准备 32 位依赖，可以先临时切换到 64 位构建，或按上面的 i386 依赖命令补齐环境
- `pkg-config --exists sdl2` 只代表当前默认架构 SDL2 可用；x86 构建需要确认 `PKG_CONFIG_LIBDIR="$LINUX_X86_SDL2_PATH" pkg-config --exists sdl2` 也能通过
- `-m32` 链接失败通常是 `gcc-multilib`、`g++-multilib`、`libc6-dev-i386` 或 `libsdl2-dev:i386` 缺失
- `build_all.sh --dry-run` 会实际做小型探测编译；如果某个组合被跳过，优先看脚本打印的 `[SKIP]` 原因

### macOS

- 构建入口：`develop-simulator.sh`；macOS 宿主机默认平台就是 `macos`
- 默认构建目录：`simulator/FloatairSimulator/build-macos-llvm`
- 默认安装目录：`install/macos-llvm`
- 平台选择：脚本会显式传入本地 `clang`，CMake 再根据当前运行平台识别为 `macos`
- 当前只支持 64 位，不支持 32 位 macOS 目标
- 如果只想构建并安装、不启动模拟器，可以传入 `--no_run`
- 如果需要指定安装目录，可以传入 `--prefix <INSTALL_DIR>`
- 如果只想检查当前 macOS 构建依赖，可以传入 `--check-platform-deps`
- SDL2 通过 CMake 查找；Homebrew 默认路径 `/opt/homebrew` 和 `/usr/local` 会自动加入查找范围
- 依赖：
  - Xcode Command Line Tools
  - `cmake`
  - `ninja`
  - `SDL2` 开发库

常见 Homebrew 安装方式如下：

```bash
xcode-select --install
brew install cmake ninja sdl2
```

默认会按当前 Mac 的原生架构构建：

- Intel Mac：`x86_64`
- Apple Silicon：`arm64`

如需指定架构，可以在手动配置时传入 `CMAKE_OSX_ARCHITECTURES`。如果 SDL2 不在 Homebrew 默认路径，可以通过 `-DSIMULATOR_SDL2_ROOT=<SDL2_ROOT>` 指定。

注意：使用 Homebrew SDL2 时，只推荐构建当前机器原生架构：Intel Mac 构建 `x86_64`，Apple Silicon 构建 `arm64`。跨架构构建和 universal build 都需要自行准备匹配架构的 SDL2；Homebrew 默认 SDL2 通常不满足这个要求。

常用脚本示例：

```bash
./simulator/FloatairSimulator/develop-simulator.sh
./simulator/FloatairSimulator/develop-simulator.sh --check-platform-deps
./simulator/FloatairSimulator/develop-simulator.sh --no_run
./simulator/FloatairSimulator/develop-simulator.sh --prefix install/macos-llvm --no_run
./simulator/FloatairSimulator/build_all.sh --dry-run
```

#### macOS 排错要点

- 如果 `clang` 不存在，先执行 `xcode-select --install`
- 如果 CMake 找不到 SDL2，优先确认 `brew install sdl2` 已完成；非 Homebrew 路径可以传 `-DSIMULATOR_SDL2_ROOT=<SDL2_ROOT>`
- Apple Silicon 上 Homebrew 默认前缀通常是 `/opt/homebrew`，Intel Mac 通常是 `/usr/local`，脚本和 `build_all.sh` 会把这两个路径加入 SDL2 查找范围
- 不建议直接用 Homebrew SDL2 做跨架构构建；需要跨架构时请准备包含目标架构的 SDL2，并在手动 CMake 配置里设置 `CMAKE_OSX_ARCHITECTURES`

### Windows 通用工具

`Windows MinGW` 和 `Windows MSVC` 两条构建路径都依赖 `cmake` 和 `ninja`，并且要求它们能在命令行里直接执行。

- CMake 下载页：`https://cmake.org/download/`
- Ninja 发布页：`https://github.com/ninja-build/ninja/releases`

安装后请确保 `cmake.exe` 和 `ninja.exe` 已加入 `PATH`，并可通过下面命令验证：

```bat
cmake --version
ninja --version
```

### Windows MinGW

- 构建入口：`develop-simulator-mingw.bat`
- 默认构建目录：`simulator\FloatairSimulator\build-mingw-x86-gcc`
- 默认安装目录：`install\mingw-x86-gcc`
- 平台选择：使用 MinGW `gcc` / `clang` 时自动识别为 `mingw`
- 脚本支持 `--arch x86|x64`、`--compiler gcc|llvm`、`--build-dir <DIR>`、`--prefix <DIR>`、`--no_run`、`--check-platform-deps` 和 `--no-pause`
- 不加任何参数时默认使用 `x86 + gcc`，会先从 `MINGW` 环境变量列出的目录查找 `i686-w64-mingw32-gcc.exe`
- `--compiler gcc` 的查找顺序是：`MINGW` 环境变量列出的目录、`PATH`、`.bat` 传给 `$FallbackDir` 的单个目录
- `--compiler llvm` 的查找顺序是：`LLVM` 环境变量列出的目录、`PATH`、`.bat` 传给 `$FallbackDir` 的单个目录
- `MINGW` 和 `LLVM` 可以像 `PATH` 一样使用分号分隔多个候选 `bin` 目录，例如 `D:\Tools\mingw\32\bin;D:\Tools\mingw\64\bin`
- 脚本不依赖目录名判断位宽，而是按目标三元组查找编译器文件名，并用编译器自报信息校验目标架构
- `.bat` 里的 `FALLBACK_DIR` 只影响最后的单目录兜底，不改变默认构建组合；MinGW 下它表示包含目标编译器的目录，MSVC 下它表示 Visual Studio 安装根目录
- 脚本会校验编译器类型：`gcc` 组合会拒绝实际为 Clang 的 wrapper，`llvm` 组合要求编译器实际为 Clang
- MinGW 下载入口：
  - mingw-w64 官方预编译工具链列表：`https://www.mingw-w64.org/downloads/`
  - `MinGW-W64-builds` 发布页：`https://github.com/niXman/mingw-builds-binaries/releases`
  - x86 脚本建议选择 `i686` + `posix` + `dwarf` + `ucrt` 这一组工具链
  - x64 手动构建可选择 `x86_64` + `posix` + `seh` + `ucrt` 这一组工具链
  - 示例文件名：`i686-15.2.0-release-posix-dwarf-ucrt-rt_v13-rev0.7z`
  - 具体 `rev` 号以发布页最新版本为准，不要写死
- 依赖：
  - `cmake`
  - `ninja`
  - MinGW GCC 工具链
  - 如需使用 MinGW Clang，需要额外安装 LLVM-MinGW 或其他带 MinGW target 的 Clang 工具链；普通 MinGW GCC 工具包通常不包含 `clang.exe`
  - 仓库内 `windows/SDL2` 提供的 SDL2 SDK

推荐把不同 MinGW 工具链放到单独环境变量里，而不是全部塞进 `PATH`：

```bat
set MINGW=D:\Tools\mingw32\bin;D:\Tools\mingw64\bin
set LLVM=D:\Tools\llvm\bin
```

这两个环境变量可以长期保留；脚本会按 `--arch` 查找对应三元组编译器，例如 x86 查 `i686-w64-mingw32-*`，x64 查 `x86_64-w64-mingw32-*`。运行时 DLL 由 CMake 根据最终编译器路径反推并拷贝，避免 x86 产物旁边拷到 x64 的 `libwinpthread-1.dll`。

如果不想配置列表，也可以把 `develop-simulator-mingw.bat` 文件头的 `FALLBACK_DIR` 指向包含目标编译器的单个目录，例如：

```bat
set "FALLBACK_DIR=D:\Tools\mingw\bin"
```

然后执行：

```bat
simulator\FloatairSimulator\develop-simulator-mingw.bat --arch x86 --compiler gcc
```

MinGW 构建脚本默认查找带目标三元组前缀的编译器，例如 `i686-w64-mingw32-gcc.exe`、`x86_64-w64-mingw32-gcc.exe`、`i686-w64-mingw32-clang.exe` 或 `x86_64-w64-mingw32-clang.exe`。排查问题时建议先用 `where <compiler>` 确认 `PATH` 里是否已有目标编译器。

脚本会检查目标架构对应的 MinGW 编译器是否存在；后续 `cmake` 配置同时也会使用同目录下配套的 `g++.exe`。通常下载完整的 MinGW 工具链后这些文件都会一起提供，不需要单独额外准备 `g++`。

常用脚本示例：

```bat
simulator\FloatairSimulator\develop-simulator-mingw.bat
simulator\FloatairSimulator\develop-simulator-mingw.bat --arch x86
simulator\FloatairSimulator\develop-simulator-mingw.bat --compiler llvm
simulator\FloatairSimulator\develop-simulator-mingw.bat --check-platform-deps
simulator\FloatairSimulator\develop-simulator-mingw.bat --arch x86 --compiler llvm --no_run
```

手动 CMake 构建时，MinGW 目标位宽由编译器决定：`i686-w64-mingw32-gcc.exe` 会走 x86，`x86_64-w64-mingw32-gcc.exe` 会走 x64。仓库内 SDL2 SDK 会按目标位宽自动选择 `windows\SDL2\lib\mingw\x86` 或 `windows\SDL2\lib\mingw\x64`。

#### Linux/macOS 交叉编译 Windows MinGW

Linux/macOS 交叉编译 Windows 模拟器时，同样只需要把 MinGW 编译器传给 CMake，平台会由编译器名称自动识别为 `mingw`。

交叉编译脚本要求以下工具在 `PATH` 中可用：

- GCC 构建：`i686-w64-mingw32-gcc` 或 `x86_64-w64-mingw32-gcc`
- LLVM 构建：优先使用 `i686-w64-mingw32-clang` 或 `x86_64-w64-mingw32-clang`
- Linux 上如果没有带三元组前缀的 `*-clang`，脚本可以使用系统 `clang` 并传入 `CMAKE_C_COMPILER_TARGET`
- macOS 上不使用系统通用 `clang` 作为 Windows GNU target fallback；如需 LLVM 构建，请安装 LLVM-MinGW 并把它的 `bin` 加入 `PATH`

Ubuntu / Debian 安装 MinGW GCC：

```bash
sudo apt update
sudo apt install mingw-w64

i686-w64-mingw32-gcc --version
x86_64-w64-mingw32-gcc --version
```

Linux 上如果希望使用系统 `clang --target=<mingw-triple>` 走 LLVM 构建，再安装 Clang / LLD：

```bash
sudo apt install clang lld
clang --target=i686-w64-mingw32 --version
clang --target=x86_64-w64-mingw32 --version
```

macOS 上常用 Homebrew 安装 MinGW GCC：

```bash
brew install mingw-w64

i686-w64-mingw32-gcc --version
x86_64-w64-mingw32-gcc --version
```

如果要在 Linux/macOS 上使用 LLVM-MinGW，推荐从 LLVM-MinGW 发布页下载对应宿主平台的预编译包：`https://github.com/mstorsjo/llvm-mingw/releases`。解压后把其中的 `bin` 目录加入 `PATH`，再验证带三元组前缀的 Clang：

```bash
export PATH="<LLVM_MINGW_ROOT>/bin:$PATH"

i686-w64-mingw32-clang --version
x86_64-w64-mingw32-clang --version
```

其中 `<LLVM_MINGW_ROOT>` 是解压后的 LLVM-MinGW 根目录。只要上述命令可执行，`develop-simulator.sh --platform mingw --compiler llvm` 就可以使用这套工具链。

如果只想检查当前 Linux/macOS 到 Windows MinGW 的交叉编译依赖，可以传入 `--check-platform-deps`：

```bash
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw --check-platform-deps
```

一键脚本默认构建 x86：

```bash
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw
```

显式 x86 构建：

```bash
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw --arch x86
```

LLVM/Clang 构建：

```bash
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw --compiler llvm
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw --compiler llvm --arch x86
```

手动配置 GCC 示例：

```bash
cmake -S . -B simulator/FloatairSimulator/build-mingw-x86-gcc -G Ninja \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-mingw-x86-gcc
```

手动配置 LLVM-MinGW 示例：

```bash
cmake -S . -B simulator/FloatairSimulator/build-mingw-x86-llvm -G Ninja \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-clang \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-mingw-x86-llvm
```

如果环境里只有通用 `clang`，则显式传入 Windows GNU target：

```bash
cmake -S . -B simulator/FloatairSimulator/build-mingw-x86-llvm -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_C_COMPILER_TARGET=i686-w64-mingw32 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-mingw-x86-llvm
```

交叉编译产物是 Windows `.exe`，脚本只负责构建和复制运行时依赖，不会在 Linux/macOS 上直接启动模拟器。如需在当前宿主运行，需要自行使用 Wine 等环境。

### Windows MSVC

- 构建入口：`develop-simulator-msvc.bat`
- 默认构建目录：`simulator\FloatairSimulator\build-msvc-x86-cl`
- 默认安装目录：`install\msvc-x86-cl`
- 平台选择：使用 `cl.exe`、Visual Studio LLVM `clang.exe` 或 `clang-cl.exe` 时自动识别为 `msvc`
- 脚本支持 `--arch x86|x64`、`--compiler cl|clang|clang-cl`、`--build-dir <DIR>`、`--prefix <DIR>`、`--no_run`、`--check-platform-deps` 和 `--no-pause`
- 默认使用 `x86 + cl`
- 依赖：
  - `cmake`
  - `ninja`
  - Visual Studio C++ 工具链，或 Visual Studio 自带 LLVM 工具链
  - 仓库内 `windows/SDL2` 提供的 SDL2 SDK
  - 在没有安装 Visual Studio 的纯运行环境上，需安装与目标位宽匹配的 Microsoft Visual C++ Redistributable

脚本会按 `--arch` 选择目标架构。如果当前终端的 Visual Studio 环境不是目标架构，脚本会重新加载 `VsDevCmd.bat -arch=<x86|x64> -host_arch=x64`。

如果当前终端里还没有可用的 Visual Studio 编译环境，脚本会优先使用 `VSDEVCMD` 环境变量或 `vswhere.exe` 查找 Visual Studio；找不到时再尝试常见安装路径。目前脚本会扫描 Visual Studio `18`、`2022` 和 `2019` 的 `Community` / `Professional` / `Enterprise` / `BuildTools` 安装目录，例如：

```bat
%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat
```

如果你的 Visual Studio 安装在其他位置，可以修改 `develop-simulator-msvc.bat` 文件头的 `FALLBACK_DIR`，让它指向 Visual Studio 安装根目录；脚本会在最后尝试 `$FallbackDir\Common7\Tools\VsDevCmd.bat`。

Visual C++ Redistributable 官方入口：

- Microsoft Learn 说明页：`https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170`
- x86 直链：`https://aka.ms/vs/17/release/vc_redist.x86.exe`
- x64 直链：`https://aka.ms/vs/17/release/vc_redist.x64.exe`

手动 CMake 构建时，MSVC 目标位宽由当前开发者命令行或编译器决定：x86 开发者命令行会走 x86，x64 开发者命令行会走 x64。仓库内 SDL2 SDK 会按目标位宽自动选择 `windows\SDL2\lib\msvc\x86` 或 `windows\SDL2\lib\msvc\x64`。

常用脚本示例：

```bat
simulator\FloatairSimulator\develop-simulator-msvc.bat
simulator\FloatairSimulator\develop-simulator-msvc.bat --arch x86
simulator\FloatairSimulator\develop-simulator-msvc.bat --compiler clang
simulator\FloatairSimulator\develop-simulator-msvc.bat --check-platform-deps
simulator\FloatairSimulator\develop-simulator-msvc.bat --arch x86 --compiler clang-cl --no_run
```

### Windows 排错要点

- 先用 `simulator\FloatairSimulator\build_all.bat --dry-run` 看当前机器会构建哪些组合；dry-run 不会执行 CMake 配置和真实编译
- 如果 MinGW 组合被跳过，先确认 `MINGW` / `LLVM` 里的目录是否包含对应三元组编译器，例如 `i686-w64-mingw32-gcc.exe` 或 `x86_64-w64-mingw32-clang.exe`
- 如果 x86 MinGW LLVM 产物启动时报 `clock_gettime64` 或类似入口点错误，优先检查 `floatair_simulator.exe` 旁边的 `libwinpthread-1.dll` 是否拷成了 x64 版本；删除旧 build 目录后用当前脚本重构建即可
- 如果 MSVC `clang` / `clang-cl` 链接出错，确认脚本输出里 `MSVC LLVM target` 是否和 `--arch` 一致：x86 应为 `i686-pc-windows-msvc`，x64 应为 `x86_64-pc-windows-msvc`

## 推荐启动方式

### Linux

Linux 下不要求先进入仓库根目录；只要执行到这个脚本即可。常见用法是先进入模拟器目录：

```bash
cd simulator/FloatairSimulator
chmod +x develop-simulator.sh
./develop-simulator.sh
```

也可以在仓库其他目录，甚至仓库外部直接执行脚本路径，例如：

```bash
./jy_app/simulator/FloatairSimulator/develop-simulator.sh
```

脚本会执行这些事情：

1. 删除旧的 `build-linux-x86-gcc`
2. 重新配置 `cmake -S . -B simulator/FloatairSimulator/build-linux-x86-gcc`
   实际会显式附带 `-DCMAKE_C_COMPILER=gcc`
3. 编译 `floatair_simulator`
4. 保留 CMake/Ninja 中间文件，方便后续增量构建和查看 `compile_commands.json`
5. 默认切到 `build-linux-x86-gcc` 目录并直接启动 `./floatair_simulator`；传入 `--no_run` 时只构建和安装，不启动

### Windows MinGW

在 Windows 下可以直接双击 `develop-simulator-mingw.bat`，也可以在命令行中执行。`.bat` 默认在退出前保留窗口；脚本调用或不想等待时传入 `--no-pause`。

```bat
simulator\FloatairSimulator\develop-simulator-mingw.bat
```

默认构建 `x86 + gcc`，输出目录为：

```text
simulator\FloatairSimulator\build-mingw-x86-gcc\floatair_simulator.exe
```

传入 `--no_run` 时只构建和安装，不启动模拟器；传入 `--check-platform-deps` 时只检查当前 MinGW 构建依赖。

### Windows MSVC

在 Windows 下可以直接双击 `develop-simulator-msvc.bat`，也可以在命令行中执行。`.bat` 默认在退出前保留窗口；脚本调用或不想等待时传入 `--no-pause`。

```bat
simulator\FloatairSimulator\develop-simulator-msvc.bat
```

默认构建 `x86 + cl`，输出目录为：

```text
simulator\FloatairSimulator\build-msvc-x86-cl\floatair_simulator.exe
```

传入 `--no_run` 时只构建和安装，不启动模拟器；传入 `--check-platform-deps` 时只检查当前 MSVC 构建依赖。

### macOS

macOS 下不要求先进入仓库根目录；常见用法是先进入模拟器目录：

```bash
cd simulator/FloatairSimulator
chmod +x develop-simulator.sh
./develop-simulator.sh
```

也可以在仓库其他目录，甚至仓库外部直接执行脚本路径，例如：

```bash
./jy_app/simulator/FloatairSimulator/develop-simulator.sh
```

脚本会执行这些事情：

1. 删除旧的 `build-macos-llvm`
2. 重新配置 `cmake -S . -B simulator/FloatairSimulator/build-macos-llvm`
   实际会显式附带 `-DCMAKE_C_COMPILER=clang`
3. 编译 `floatair_simulator`
4. 保留 CMake/Ninja 中间文件，方便后续增量构建和查看 `compile_commands.json`
5. 默认切到 `build-macos-llvm` 目录并直接启动 `./floatair_simulator`；传入 `--no_run` 时只构建和安装，不启动

## 手动构建与运行

如果只想手动编译，不走一键脚本，可以直接在仓库根目录执行 CMake。

### Linux

使用 `gcc`：

```bash
cmake -S . -B simulator/FloatairSimulator/build-linux-x86-gcc -G Ninja \
  -DCMAKE_C_COMPILER=gcc \
  -DARCH=x86 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-linux-x86-gcc
cd simulator/FloatairSimulator/build-linux-x86-gcc
./floatair_simulator
```

使用 `clang`：

```bash
cmake -S . -B simulator/FloatairSimulator/build-linux-x86-llvm -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DARCH=x86 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-linux-x86-llvm
cd simulator/FloatairSimulator/build-linux-x86-llvm
./floatair_simulator
```

### macOS

使用当前 Mac 原生架构：

```bash
cmake -S . -B simulator/FloatairSimulator/build-macos-llvm -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-macos-llvm
cd simulator/FloatairSimulator/build-macos-llvm
./floatair_simulator
```

如需跨架构或 universal build，可以设置 `CMAKE_OSX_ARCHITECTURES`，但必须确保 SDL2 也包含对应架构；使用 Homebrew SDL2 时不推荐这样做。

### MinGW

使用 `MinGW GCC` x86：

```bash
cmake -S . -B simulator/FloatairSimulator/build-mingw-x86-gcc -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="<MINGW_BIN>/i686-w64-mingw32-gcc.exe"
cmake --build simulator/FloatairSimulator/build-mingw-x86-gcc
```

其中 `<MINGW_BIN>` 表示本机 MinGW 工具链的 `bin` 目录；如果该目录已经加入 `PATH`，也可以直接写短名：

```bash
cmake -S . -B build-mingw-x86-gcc -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build-mingw-x86-gcc --target floatair_simulator

cmake -S . -B build-mingw-x86-llvm -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build-mingw-x86-llvm --target floatair_simulator
```

注意：上面的 `clang` 指的是已额外安装并可生成 MinGW ABI 产物的 Clang，不是普通 MinGW GCC 工具包自带组件。

MinGW Clang 可从 LLVM-MinGW 发布页下载：`https://github.com/mstorsjo/llvm-mingw/releases`。x86 可选择 `ucrt` + `i686` 包，x64 可选择 `ucrt` + `x86_64` 包；具体日期版本以发布页最新版本为准。

### MSVC

使用 `cl.exe`：

```bat
cmake -S . -B simulator\FloatairSimulator\build-msvc-x86-cl -G Ninja ^
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator\FloatairSimulator\build-msvc-x86-cl
```

上面的命令会使用当前开发者命令行里的目标位宽；建议先进入 x86 Developer Command Prompt，或先执行：

```bat
VsDevCmd.bat -arch=x86 -host_arch=x64
```

使用 Visual Studio LLVM 的 `clang.exe` / `clang-cl.exe`：

```bat
cmake -S . -B simulator\FloatairSimulator\build-msvc-x86-clang -G Ninja ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_C_COMPILER="<VS_LLVM_BIN>\clang.exe" ^
  -DCMAKE_C_COMPILER_TARGET=i686-pc-windows-msvc
cmake --build simulator\FloatairSimulator\build-msvc-x86-clang --target floatair_simulator

cmake -S . -B simulator\FloatairSimulator\build-msvc-x86-clang-cl -G Ninja ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_C_COMPILER="<VS_LLVM_BIN>\clang-cl.exe" ^
  -DCMAKE_C_COMPILER_TARGET=i686-pc-windows-msvc
cmake --build simulator\FloatairSimulator\build-msvc-x86-clang-cl --target floatair_simulator
```

其中 `<VS_LLVM_BIN>` 表示本机 Visual Studio LLVM 的 `VC\Tools\Llvm\bin`、`VC\Tools\Llvm\x64\bin` 或 `VC\Tools\Llvm\x86\bin` 目录；如果该目录已经加入 `PATH`，也可以直接写 `clang` 或 `clang-cl`。x64 LLVM 构建时把 `CMAKE_C_COMPILER_TARGET` 改为 `x86_64-pc-windows-msvc`。

手动运行时请注意两点：

1. 从对应 `build-*` 目录启动，确保能读到同目录下的 `simulator_socket.conf`
2. 不要删除构建后自动生成的 `jyt_d/` 目录

## 安装到独立目录

一键脚本构建后会自动执行 `ninja install`，把可运行文件和资源集中安装到项目根目录的 `install/` 目录；`--prefix` 用于覆盖默认安装目录：

```bash
./simulator/FloatairSimulator/develop-simulator.sh \
  --arch x86 \
  --no_run
```

Windows MinGW 示例：

```bat
simulator\FloatairSimulator\develop-simulator-mingw.bat --arch x86 --no_run
```

Windows MSVC 示例：

```bat
simulator\FloatairSimulator\develop-simulator-msvc.bat --arch x86 --no_run
```

手动 CMake 构建时，配置阶段传入 `CMAKE_INSTALL_PREFIX`，再执行 `ninja install`：

```bash
cmake -S . -B simulator/FloatairSimulator/build-linux-x86-gcc -G Ninja \
  -DCMAKE_C_COMPILER=gcc \
  -DARCH=x86 \
  -DCMAKE_INSTALL_PREFIX=install/linux-x86-gcc
ninja -C simulator/FloatairSimulator/build-linux-x86-gcc
ninja -C simulator/FloatairSimulator/build-linux-x86-gcc install
```

安装目录会包含：

- `floatair_simulator` / `floatair_simulator.exe`
- `simulator_socket.conf`
- `simulator_event_panel.py`
- `jyt_d/`
- `romfs/`
- Windows 下还会安装 `SDL2.dll` 和 `libwinpthread-1.dll`

## OS 事件面板

`simulator_event_panel.py` 是一个基于 `tkinter` 的辅助工具，用来通过模拟器内部的 FIFO / Windows 命名管道主动注入系统事件。

当前面板内置了这些常用操作：

- Host 连接/断连
- 低电、充电、SOC 调整
- 佩戴/摘下
- 单击、双击、三击、长按、滑动
- IMU 点击和抬头/低头
- 时间同步
- 来电振铃、接通、挂断

Python 依赖说明：

- 这个脚本没有第三方 `pip` 依赖
- 真正需要保证的是当前 Python 环境包含 `tkinter`

安装和自检方式：

```bash
cd simulator/FloatairSimulator
python3 -m tkinter
```

如果 `python3 -m tkinter` 无法启动，通常说明当前系统缺少 Tk 支持包，例如：

```bash
# Debian / Ubuntu
sudo apt install python3-tk

# Fedora
sudo dnf install python3-tkinter
```

Windows 通常使用 python.org 官方安装包即可，安装时确保勾选 `tcl/tk and IDLE` 组件。安装后可用下面命令自检：

```bat
py -m tkinter
```

macOS 推荐两种方式：

```bash
# python.org 官方安装包通常自带 Tk
python3 -m tkinter

# 如果使用 Homebrew Python，安装与当前 Python 版本匹配的 Tk 包
brew search python-tk
brew install python-tk@<PYTHON_VERSION>
```

启动方式：

```bash
cd simulator/FloatairSimulator
python3 simulator_event_panel.py
```

注意事项：

1. 启动顺序本身没有强制要求，可以先开面板，也可以先开 `floatair_simulator`
2. 但只有在模拟器已经启动、FIFO / Named pipe 已创建后，面板里的事件发送才会成功；否则会提示 FIFO / Named pipe 不存在
3. Linux / macOS 默认使用 `/tmp/floatair_sim_event_fifo`
4. Windows 默认使用 `\\.\pipe\floatair_sim_event_fifo`
5. 通道路径由模拟器端固定创建，事件面板不提供自定义参数

## 产物说明

脚本默认不会生成“单文件打包版模拟器”，而是直接生成并运行构建目录下的本地可执行文件：

- Linux：`simulator/FloatairSimulator/build-linux-x86-gcc/floatair_simulator`
- macOS：`simulator/FloatairSimulator/build-macos-llvm/floatair_simulator`
- MinGW：`simulator/FloatairSimulator/build-mingw-x86-gcc/floatair_simulator.exe`
- MSVC：`simulator/FloatairSimulator/build-msvc-x86-cl/floatair_simulator.exe`

手动构建时产物位于配置命令 `-B` 指定的目录，例如 `build-linux-x86-gcc` 或 `build-mingw-x86-llvm`。

构建成功后，运行目录里通常至少会包含：

- `floatair_simulator` / `floatair_simulator.exe`
- `simulator_socket.conf`
- `jyt_d/`
- Windows 下会包含 `SDL2.dll`
- Windows 下还可能包含其他运行时 DLL
