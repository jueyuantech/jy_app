# FloatairSimulator

Chinese version: [FloatairSimulator_cn.md](FloatairSimulator_cn.md)

`FloatairSimulator` is the desktop simulator entry point for the app project. It supports `linux`, `macos`, `mingw`, and `msvc` build platforms. The executable is always named `floatair_simulator`, or `floatair_simulator.exe` on Windows.

This document follows the scripts and CMake logic in the repository. It focuses on how to build, how to run, and where runtime dependencies are placed.

## Directory Overview

```text
FloatairSimulator/
|-- README.md
|-- main.c
|-- sys_adapter.c
|-- floatair_lcd.c
|-- floatair_fs.c
|-- lv_port_fs.c                 # LVGL filesystem port
|-- sim_socket.c
|-- simulator_event_fifo.c       # System event FIFO / named pipe input
|-- simulator_platform.c         # Platform dispatch layer
|-- simulator_socket.conf        # Simulator TCP target configuration
|-- simulator_event_panel.py     # OS event injection panel
|-- build_all.sh                 # Detect and build all supported combinations on Linux/macOS
|-- build_all.bat                # Detect and build all supported combinations on Windows
|-- develop-simulator.sh         # Unified Linux/macOS/Windows MinGW cross-build entry
|-- develop-simulator-mingw.bat  # Windows MinGW entry, forwarded to develop-simulator.ps1
|-- develop-simulator-msvc.bat   # Windows MSVC entry, forwarded to develop-simulator.ps1
|-- develop-simulator.ps1        # Shared Windows MinGW/MSVC build logic
|-- linux/
`-- windows/
```

## Runtime Logic

The simulator depends on two external runtime resources.

1. `jyt_d/`

   After the build completes, CMake copies `lfsd/` from the repository root to the output directory and creates the runtime resource directory `jyt_d/`.

   Paths such as `/jyt_d/...` inside the simulator are mapped to the `jyt_d/` directory next to the executable.

2. `simulator_socket.conf`

   The file contains three lines:

   - Line 1: host address, default `127.0.0.1`
   - Line 2: port, default `24680`
   - Line 3: display layout, `0` for side-by-side, `1` for vertical stacking, `2` for single-eye display

   CMake copies `simulator_socket.conf` from the source directory to the build output directory. When reading the configuration, the simulator first checks the executable directory and then falls back to the current working directory. The safest launch method is to use the repository scripts, or `cd` into the corresponding `build-*` directory before running the simulator.

   The default source configuration is:

   ```text
   127.0.0.1
   24680
   2
   ```

   If the third line is missing or invalid, the code falls back to `1`, which means vertically stacked dual-eye output.

By default, the simulator keeps reconnecting to `127.0.0.1:24680` for joint debugging with the phone-side test service. To connect to another address or port, edit `simulator_socket.conf`.

## Android Emulator Debugging

When debugging the Windows desktop simulator with an Android emulator, first let the phone-side test service inside LDPlayer, BlueStacks, or another Android emulator listen on `24680`. Then use `forward-jyapp-simulator.ps1` to forward the local PC address `127.0.0.1:24680` to the selected Android emulator device. With this setup, `floatair_simulator` can keep the default `simulator_socket.conf`.

### LDPlayer / BlueStacks Preparation

If there is no usable `adb.exe` on the system, download Google's official Android SDK Platform-Tools:

- Download page: `https://developer.android.com/tools/releases/platform-tools`
- Windows direct link: `https://dl.google.com/android/repository/platform-tools-latest-windows.zip`

After extracting it, add the `platform-tools` directory to `PATH`, or pass the full `adb.exe` path with `-Adb <ADB_EXE_PATH>` when running the script.

Start the Android emulator first, then check whether `adb devices` can see it:

```bat
adb devices
```

Common device names include `emulator-5554`, `127.0.0.1:5555`, `localhost:5555`, or vendor-specific names. Use the actual output from `adb devices`.

If `adb devices` does not list LDPlayer or BlueStacks, connect to the emulator's ADB address first. The port can vary by version and multi-instance setup, so prefer the emulator settings page or multi-instance manager. Common commands:

```bat
adb connect 127.0.0.1:5555
adb devices
```

For BlueStacks, if the system `adb` does not work, check whether BlueStacks' bundled ADB is usable and pass it through `-Adb`:

```bat
"C:\Program Files\BlueStacks_nxt\HD-Adb.exe" devices
```

### forward-jyapp-simulator.ps1

`forward-jyapp-simulator.ps1` does the following:

1. Optionally runs `adb connect <address>`
2. Reads currently online ADB devices
3. Lets the user choose a device when no device is specified
4. Removes existing local port forwarding on that device
5. Creates `local tcp:<Port> -> device tcp:<Port>` forwarding
6. Prints the current `adb forward --list`

Script parameters:

| Parameter | Default | Description |
| --- | --- | --- |
| `-Device` | Empty | Device name from `adb devices`. If empty, the script lists devices and asks the user to choose. |
| `-Port` | `24680` | TCP port used by both PC and Android device. |
| `-Adb` | `adb` | ADB executable. A full path can be used, such as BlueStacks' `HD-Adb.exe`. |
| `-Connect` | Empty | Run `adb connect <address>` before device selection. Useful when LDPlayer or BlueStacks does not appear automatically. |

Common commands:

```powershell
# Enter the simulator directory
cd jy_app\simulator\FloatairSimulator

# List ADB devices and choose one. Forward 24680 by default.
.\forward-jyapp-simulator.ps1

# Specify a device name
.\forward-jyapp-simulator.ps1 -Device emulator-5554

# Connect LDPlayer / BlueStacks ADB first, then choose a device
.\forward-jyapp-simulator.ps1 -Connect 127.0.0.1:5555

# Use BlueStacks bundled ADB
.\forward-jyapp-simulator.ps1 -Adb "C:\Program Files\BlueStacks_nxt\HD-Adb.exe" -Connect 127.0.0.1:5555

# If the Android-side service uses another port, update simulator_socket.conf too
.\forward-jyapp-simulator.ps1 -Port 24680
```

Successful output looks like this:

```text
Using adb device: emulator-5554
Forwarding local tcp:24680 -> device tcp:24680

Current forward list:
emulator-5554 tcp:24680 tcp:24680
```

### Recommended Debugging Order

1. Start LDPlayer / BlueStacks.
2. Start the Android app or test service and make sure device-side `24680` is listening.
3. Run `forward-jyapp-simulator.ps1`.
4. Start `floatair_simulator`.

If the desktop simulator is already running, you can run `forward-jyapp-simulator.ps1` again. The simulator keeps reconnecting to `127.0.0.1:24680`.

Common checks:

- `No adb device found`: start the Android emulator first, then run `adb devices`; use `-Connect 127.0.0.1:<port>` if needed.
- Multiple devices are listed: use `-Device <device name>` to avoid choosing the wrong instance.
- Still cannot connect after forwarding: check that the Android-side service port matches line 2 in `simulator_socket.conf`; the default is `24680`.
- BlueStacks device is not found: try `-Adb "C:\Program Files\BlueStacks_nxt\HD-Adb.exe"` to use BlueStacks' bundled ADB.

## Platforms and Requirements

A 32-bit simulator environment is recommended because it is closer to the pointer width and data layout of the real ARMv7 device. The 64-bit environment is mainly for UI preview, interaction preview, and daily debugging, and should not be the only verification path.

Supported compiler boundaries:

| Platform | Supported compilers | Description |
| --- | --- | --- |
| `linux` | `gcc` / `clang` | Native Linux simulator. Defaults to 32-bit build, switchable through `ARCH`. |
| `macos` | AppleClang / `clang` | Native macOS simulator. 64-bit only. |
| `mingw` | MinGW `gcc` / MinGW `clang` | Windows MinGW build. Supports native Windows builds and Linux/macOS cross-builds. Supports x86 / x64 and links MinGW ABI `.dll.a`. |
| `msvc` | `cl` / Visual Studio `clang` / `clang-cl` | Supports x86 / x64 and links MSVC ABI `.lib`. |

Verified Windows combinations include MinGW `gcc` / `clang`, MSVC `cl`, Visual Studio `clang` / `clang-cl`, and both x86 / x64. Linux/macOS can cross-build Windows MinGW artifacts through MinGW `gcc` or LLVM/Clang. CMake infers the platform from the compiler name or `CMAKE_C_COMPILER_TARGET`; no additional toolchain file is required.

On Windows, distinguish the command-line frontend from the target ABI:

- Visual Studio LLVM `clang.exe` uses GNU-like arguments such as `-I`, `-D`, and `-W...`, but links through `lld-link.exe` and MSVC `.lib`, so it belongs to `msvc`.
- `clang-cl.exe` on a Windows host or with an explicit `windows-msvc` target uses MSVC-like arguments such as `/I`, `/D`, and `/wd...`, so it also belongs to `msvc`.
- MinGW / LLVM-MinGW `clang.exe` uses the MinGW ABI and belongs to `mingw`.
- MinGW toolchains on Linux/macOS generate Windows GNU artifacts. The project recognizes compilers such as `i686-w64-mingw32-gcc`, `x86_64-w64-mingw32-gcc`, or Clang with a `windows-gnu` target as `mingw`.
- macOS LLVM packages can provide `clang-cl`, but that is still an MSVC-style frontend and will parse `/Users/...` paths as MSVC options, so it cannot be used for native macOS simulator builds.

### Batch Build

On Linux/macOS, use the unified entry to detect the current environment and build all supported combinations:

```bash
./simulator/FloatairSimulator/build_all.sh
```

On Linux, the script checks:

- Native `gcc` and `clang`
- MinGW `gcc`
- MinGW `clang`, including prefixed `*-clang` or usable `clang --target=<mingw-triple>`
- Linux SDL2 x86 and x64 development environments

On macOS, the script checks:

- Native `clang`
- macOS SDL2 development environment
- MinGW `gcc`
- MinGW `clang`, including prefixed `*-clang`

Only combinations that pass dependency checks are built. Missing combinations are skipped with a printed reason. Native Linux/macOS builds automatically pass `--no_run`, so they only build and install artifacts without launching the simulator. Each combination installs into the corresponding `install/linux-*`, `install/mingw-*`, or `install/macos-*` directory under the project root.

To preview which combinations would be built:

```bash
./simulator/FloatairSimulator/build_all.sh --dry-run
```

On Windows, use:

```bat
simulator\FloatairSimulator\build_all.bat
```

When running the scripts interactively, they prompt for the OS SDK archive path after product selection; press Enter to use the newest `.os_sdk_cache/` entry. From the command line, the 7z path can also be passed as the first positional argument, for example `develop-simulator.sh /home/user/os_sdk.7z` or `develop-simulator-mingw.bat C:\path\os_sdk.7z`.

Pass `--no-pause` if you do not want the script window to remain open at the end. `build_all.bat` also passes `--no-pause` automatically when calling `develop-simulator.ps1`.

The Windows script checks:

- MinGW `gcc`: x86 searches for `i686-w64-mingw32-gcc.exe`, x64 searches for `x86_64-w64-mingw32-gcc.exe`
- MinGW `clang`: x86 searches for `i686-w64-mingw32-clang.exe`, x64 searches for `x86_64-w64-mingw32-clang.exe`
- MSVC `cl`
- Visual Studio LLVM `clang`
- Visual Studio LLVM `clang-cl`

MinGW `gcc` is searched first from directories listed in the `MINGW` environment variable. MinGW `clang` is searched first from directories listed in `LLVM`. If not found, the script falls back to `PATH` and the single fallback directory variable. Only combinations that pass checks are built. Batch builds automatically pass `--no_run`, so they only build and install artifacts without launching the simulator. Each combination installs into `install\mingw-*` or `install\msvc-*` under the project root.

To preview Windows combinations:

```bat
simulator\FloatairSimulator\build_all.bat --dry-run
```

### Ubuntu / Linux

- Build entry: `develop-simulator.sh`; on a Linux host, the default platform is `linux`.
- Default build directory: `simulator/FloatairSimulator/build-linux-x86-gcc`.
- Default install directory: `install/linux-x86-gcc`.
- Platform selection: the script explicitly passes native `gcc`, and CMake recognizes the platform as `linux`.
- Default `ARCH=x86`, compiled with `-m32`, so a 32-bit compile/link environment is required.
- For a 64-bit native build, the script can switch to x64; manual CMake configuration still needs `-DARCH=amd64`.
- Linux x86 uses `pkg-config` to find i386 SDL2. The default path is `/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig`. If i386 `sdl2.pc` is missing, `build_all.sh` skips that combination.
- Script x64 maps to CMake `amd64` and uses `find_package(SDL2)` to find system SDL2.
- Ubuntu 20.04 is recommended for Linux simulator development. x86 and x64 native dependencies are compatible there.
- On other environments, do not force-install 32-bit SDL2 for Linux x86 builds if package conflicts appear. If `libsdl2-dev:i386` cannot be safely installed, let `build_all.sh` skip that combination or use a container / VM.
- Dependencies:
  - `cmake`
  - `ninja`
  - `gcc`
  - SDL2 development library
  - `pthread`

Linux dependency commands below target Ubuntu 20.04 / Debian-based systems. Other distributions are not verified.

For x64 native builds only:

```bash
sudo apt update
sudo apt install cmake ninja-build gcc clang libsdl2-dev
```

For the default x86 build, enable i386 and install 32-bit compile/link dependencies and i386 SDL2. Before installing `libsdl2-dev:i386` outside Ubuntu 20.04, carefully inspect the apt plan. If it would remove core packages such as `ubuntu-desktop-minimal`, `gnome-shell`, `network-manager`, or `python3`, skip native Linux x86 or use a container / VM:

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install gcc-multilib g++-multilib libc6-dev-i386 libsdl2-dev:i386
```

To let `build_all.sh` detect native `gcc` / `clang` and MinGW cross-builds, install MinGW too:

```bash
sudo apt install mingw-w64
```

If the x86 SDL2 `pkgconfig` file is not in the default location, override it during CMake configuration:

```bash
cmake -S . -B simulator/FloatairSimulator/build-linux-x86-gcc -G Ninja \
  -DCMAKE_C_COMPILER=gcc \
  -DARCH=x86 \
  -DLINUX_X86_SDL2_PATH="<SDL2_I386_PKGCONFIG_DIR>:/usr/share/pkgconfig"
```

Common script examples:

```bash
./simulator/FloatairSimulator/develop-simulator.sh
./simulator/FloatairSimulator/develop-simulator.sh --compiler llvm
./simulator/FloatairSimulator/develop-simulator.sh --check-platform-deps
./simulator/FloatairSimulator/develop-simulator.sh --arch x86 --no_run
./simulator/FloatairSimulator/develop-simulator.sh --arch x86 --prefix install/linux-x86-gcc --no_run
```

Manual LLVM/Clang example:

```bash
cmake -S . -B simulator/FloatairSimulator/build-linux-x86-llvm -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DARCH=x86 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-linux-x86-llvm
```

Linux troubleshooting:

- The default script uses x86. If 32-bit dependencies are not ready, temporarily switch to 64-bit or install the i386 dependencies above.
- `pkg-config --exists sdl2` only checks the current default architecture. For x86 builds, also verify `PKG_CONFIG_LIBDIR="$LINUX_X86_SDL2_PATH" pkg-config --exists sdl2`.
- `-m32` link failures usually mean `gcc-multilib`, `g++-multilib`, `libc6-dev-i386`, or `libsdl2-dev:i386` is missing.
- `build_all.sh --dry-run` performs small probe builds. If a combination is skipped, check the printed `[SKIP]` reason first.

### macOS

- Build entry: `develop-simulator.sh`; on a macOS host, the default platform is `macos`.
- Default build directory: `simulator/FloatairSimulator/build-macos-llvm`.
- Default install directory: `install/macos-llvm`.
- Platform selection: the script explicitly passes native `clang`, and CMake recognizes the platform as `macos`.
- Only 64-bit targets are supported.
- Pass `--no_run` to build and install without launching.
- Pass `--prefix <INSTALL_DIR>` to override the install directory.
- Pass `--check-platform-deps` to check current macOS build dependencies only.
- SDL2 is found through CMake. Homebrew default paths `/opt/homebrew` and `/usr/local` are added automatically.
- Dependencies:
  - Xcode Command Line Tools
  - `cmake`
  - `ninja`
  - SDL2 development library

Common Homebrew setup:

```bash
xcode-select --install
brew install cmake ninja sdl2
```

By default, the build uses the current Mac's native architecture:

- Intel Mac: `x86_64`
- Apple Silicon: `arm64`

To specify an architecture manually, pass `CMAKE_OSX_ARCHITECTURES`. If SDL2 is not in a Homebrew default path, pass `-DSIMULATOR_SDL2_ROOT=<SDL2_ROOT>`.

When using Homebrew SDL2, build only the machine's native architecture. Cross-architecture and universal builds require a matching SDL2 package prepared manually.

Common script examples:

```bash
./simulator/FloatairSimulator/develop-simulator.sh
./simulator/FloatairSimulator/develop-simulator.sh --check-platform-deps
./simulator/FloatairSimulator/develop-simulator.sh --no_run
./simulator/FloatairSimulator/develop-simulator.sh --prefix install/macos-llvm --no_run
./simulator/FloatairSimulator/build_all.sh --dry-run
```

macOS troubleshooting:

- If `clang` does not exist, run `xcode-select --install`.
- If CMake cannot find SDL2, confirm `brew install sdl2` is complete; use `-DSIMULATOR_SDL2_ROOT=<SDL2_ROOT>` for non-Homebrew paths.
- Apple Silicon Homebrew usually uses `/opt/homebrew`; Intel Mac usually uses `/usr/local`.
- Do not use Homebrew SDL2 for cross-architecture builds unless the SDL2 package includes the target architecture.

### Windows Common Tools

Both Windows MinGW and Windows MSVC build paths require `cmake` and `ninja`, and they must be directly executable from the command line.

- CMake download page: `https://cmake.org/download/`
- Ninja release page: `https://github.com/ninja-build/ninja/releases`

Verify after installation:

```bat
cmake --version
ninja --version
```

### Windows MinGW

- Build entry: `develop-simulator-mingw.bat`.
- Default build directory: `simulator\FloatairSimulator\build-mingw-x86-gcc`.
- Default install directory: `install\mingw-x86-gcc`.
- Platform selection: MinGW `gcc` / `clang` is recognized as `mingw`.
- Supported options: `--arch x86|x64`, `--compiler gcc|llvm`, `--build-dir <DIR>`, `--prefix <DIR>`, `--no_run`, `--check-platform-deps`, and `--no-pause`.
- Defaults to `x86 + gcc`. It first searches for `i686-w64-mingw32-gcc.exe` in directories listed by the `MINGW` environment variable.
- `--compiler gcc` search order: `MINGW`, `PATH`, then the single fallback directory passed by the `.bat`.
- `--compiler llvm` search order: `LLVM`, `PATH`, then the single fallback directory passed by the `.bat`.
- `MINGW` and `LLVM` can contain multiple `bin` directories separated by semicolons, like `D:\Tools\mingw\32\bin;D:\Tools\mingw\64\bin`.
- The script does not infer bitness from directory names. It searches by target triple compiler name and validates the target architecture from compiler output.
- The `.bat` `FALLBACK_DIR` only affects the final single-directory fallback. It does not change the default build combination.
- The script validates compiler kind: `gcc` combinations reject Clang wrappers, while `llvm` combinations require the compiler to actually be Clang.
- Runtime SDL2 SDK is provided under `windows/SDL2` in the repository.

Recommended environment variables:

```bat
set MINGW=D:\Tools\mingw32\bin;D:\Tools\mingw64\bin
set LLVM=D:\Tools\llvm\bin
```

The script looks for the target triple according to `--arch`: x86 uses `i686-w64-mingw32-*`, x64 uses `x86_64-w64-mingw32-*`. Runtime DLLs are copied based on the final compiler path to avoid copying the wrong `libwinpthread-1.dll`.

If you do not want list-style environment variables, point `FALLBACK_DIR` in `develop-simulator-mingw.bat` to a single directory containing the target compiler:

```bat
set "FALLBACK_DIR=D:\Tools\mingw\bin"
```

Then run:

```bat
simulator\FloatairSimulator\develop-simulator-mingw.bat --arch x86 --compiler gcc
```

Common script examples:

```bat
simulator\FloatairSimulator\develop-simulator-mingw.bat
simulator\FloatairSimulator\develop-simulator-mingw.bat --arch x86
simulator\FloatairSimulator\develop-simulator-mingw.bat --compiler llvm
simulator\FloatairSimulator\develop-simulator-mingw.bat --check-platform-deps
simulator\FloatairSimulator\develop-simulator-mingw.bat --arch x86 --compiler llvm --no_run
```

For manual CMake builds, the MinGW target bitness is determined by the compiler: `i686-w64-mingw32-gcc.exe` selects x86, and `x86_64-w64-mingw32-gcc.exe` selects x64. The repository SDL2 SDK selects `windows\SDL2\lib\mingw\x86` or `windows\SDL2\lib\mingw\x64` automatically.

### Linux/macOS Cross-Build to Windows MinGW

For cross-building the Windows simulator from Linux/macOS, pass a MinGW compiler to CMake. The platform is recognized as `mingw` from the compiler name.

Required tools in `PATH`:

- GCC build: `i686-w64-mingw32-gcc` or `x86_64-w64-mingw32-gcc`
- LLVM build: preferably `i686-w64-mingw32-clang` or `x86_64-w64-mingw32-clang`
- On Linux, if prefixed `*-clang` is missing, the script can use system `clang` with `CMAKE_C_COMPILER_TARGET`
- On macOS, system generic `clang` is not used as a Windows GNU target fallback; install LLVM-MinGW for LLVM builds

Ubuntu / Debian MinGW GCC installation:

```bash
sudo apt update
sudo apt install mingw-w64

i686-w64-mingw32-gcc --version
x86_64-w64-mingw32-gcc --version
```

Linux system Clang with Windows GNU target:

```bash
sudo apt install clang lld
clang --target=i686-w64-mingw32 --version
clang --target=x86_64-w64-mingw32 --version
```

macOS MinGW GCC through Homebrew:

```bash
brew install mingw-w64

i686-w64-mingw32-gcc --version
x86_64-w64-mingw32-gcc --version
```

For LLVM-MinGW on Linux/macOS, download a prebuilt package from `https://github.com/mstorsjo/llvm-mingw/releases`, add its `bin` directory to `PATH`, and verify:

```bash
export PATH="<LLVM_MINGW_ROOT>/bin:$PATH"

i686-w64-mingw32-clang --version
x86_64-w64-mingw32-clang --version
```

Check cross-build dependencies:

```bash
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw --check-platform-deps
```

Common commands:

```bash
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw --arch x86
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw --compiler llvm
./simulator/FloatairSimulator/develop-simulator.sh --platform mingw --compiler llvm --arch x86
```

Manual GCC example:

```bash
cmake -S . -B simulator/FloatairSimulator/build-mingw-x86-gcc -G Ninja \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-mingw-x86-gcc
```

Manual LLVM-MinGW example:

```bash
cmake -S . -B simulator/FloatairSimulator/build-mingw-x86-llvm -G Ninja \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-clang \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-mingw-x86-llvm
```

Generic Clang with explicit target:

```bash
cmake -S . -B simulator/FloatairSimulator/build-mingw-x86-llvm -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_C_COMPILER_TARGET=i686-w64-mingw32 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-mingw-x86-llvm
```

The cross-build output is a Windows `.exe`. The script builds and copies runtime dependencies only; it does not launch the simulator on Linux/macOS. Use Wine or another environment if you need to run it on the host.

### Windows MSVC

- Build entry: `develop-simulator-msvc.bat`.
- Default build directory: `simulator\FloatairSimulator\build-msvc-x86-cl`.
- Default install directory: `install\msvc-x86-cl`.
- Platform selection: `cl.exe`, Visual Studio LLVM `clang.exe`, and `clang-cl.exe` are recognized as `msvc`.
- Supported options: `--arch x86|x64`, `--compiler cl|clang|clang-cl`, `--build-dir <DIR>`, `--prefix <DIR>`, `--no_run`, `--check-platform-deps`, and `--no-pause`.
- Defaults to `x86 + cl`.
- Dependencies:
  - `cmake`
  - `ninja`
  - Visual Studio C++ toolchain, or Visual Studio bundled LLVM toolchain
  - SDL2 SDK under `windows/SDL2`
  - On pure runtime machines without Visual Studio, install the Microsoft Visual C++ Redistributable matching the target bitness

The script selects target architecture according to `--arch`. If the current terminal's Visual Studio environment does not match the target architecture, the script reloads `VsDevCmd.bat -arch=<x86|x64> -host_arch=x64`.

If the current terminal has no Visual Studio environment, the script searches through `VSDEVCMD`, `vswhere.exe`, and common Visual Studio install paths. It checks Visual Studio `18`, `2022`, and `2019`, including `Community`, `Professional`, `Enterprise`, and `BuildTools`.

If Visual Studio is installed elsewhere, update `FALLBACK_DIR` at the top of `develop-simulator-msvc.bat` to the Visual Studio install root. The script will try `$FallbackDir\Common7\Tools\VsDevCmd.bat`.

Visual C++ Redistributable links:

- Microsoft Learn page: `https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170`
- x86 direct link: `https://aka.ms/vs/17/release/vc_redist.x86.exe`
- x64 direct link: `https://aka.ms/vs/17/release/vc_redist.x64.exe`

Common script examples:

```bat
simulator\FloatairSimulator\develop-simulator-msvc.bat
simulator\FloatairSimulator\develop-simulator-msvc.bat --arch x86
simulator\FloatairSimulator\develop-simulator-msvc.bat --compiler clang
simulator\FloatairSimulator\develop-simulator-msvc.bat --check-platform-deps
simulator\FloatairSimulator\develop-simulator-msvc.bat --arch x86 --compiler clang-cl --no_run
```

Windows troubleshooting:

- Run `simulator\FloatairSimulator\build_all.bat --dry-run` first to see which combinations are available on the current machine.
- If a MinGW combination is skipped, check whether `MINGW` / `LLVM` directories contain the corresponding target triple compiler, such as `i686-w64-mingw32-gcc.exe` or `x86_64-w64-mingw32-clang.exe`.
- If an x86 MinGW LLVM artifact reports `clock_gettime64` or a similar missing entry point at startup, check whether `libwinpthread-1.dll` next to `floatair_simulator.exe` is accidentally the x64 version. Delete the old build directory and rebuild with the current script.
- If MSVC `clang` / `clang-cl` fails to link, check that `MSVC LLVM target` in the script output matches `--arch`: x86 should be `i686-pc-windows-msvc`, x64 should be `x86_64-pc-windows-msvc`.

## Recommended Launch Methods

### Linux

You do not have to enter the repository root. Common usage from the simulator directory:

```bash
cd simulator/FloatairSimulator
chmod +x develop-simulator.sh
./develop-simulator.sh
```

You can also run the script path from another directory:

```bash
./jy_app/simulator/FloatairSimulator/develop-simulator.sh
```

The script:

1. Removes the old `build-linux-x86-gcc`.
2. Reconfigures `cmake -S . -B simulator/FloatairSimulator/build-linux-x86-gcc` with `-DCMAKE_C_COMPILER=gcc`.
3. Builds `floatair_simulator`.
4. Keeps CMake/Ninja intermediate files for incremental builds and `compile_commands.json`.
5. By default, enters `build-linux-x86-gcc` and launches `./floatair_simulator`. With `--no_run`, it only builds and installs.

### Windows MinGW

On Windows, double-click `develop-simulator-mingw.bat` or run it from the command line. The `.bat` keeps the window open by default; pass `--no-pause` when scripting or when you do not want to wait.

```bat
simulator\FloatairSimulator\develop-simulator-mingw.bat
```

The default build is `x86 + gcc`, output:

```text
simulator\FloatairSimulator\build-mingw-x86-gcc\floatair_simulator.exe
```

With `--no_run`, the script only builds and installs. With `--check-platform-deps`, it only checks MinGW build dependencies.

### Windows MSVC

On Windows, double-click `develop-simulator-msvc.bat` or run it from the command line. The `.bat` keeps the window open by default; pass `--no-pause` when scripting or when you do not want to wait.

```bat
simulator\FloatairSimulator\develop-simulator-msvc.bat
```

The default build is `x86 + cl`, output:

```text
simulator\FloatairSimulator\build-msvc-x86-cl\floatair_simulator.exe
```

With `--no_run`, the script only builds and installs. With `--check-platform-deps`, it only checks MSVC build dependencies.

### macOS

Common usage from the simulator directory:

```bash
cd simulator/FloatairSimulator
chmod +x develop-simulator.sh
./develop-simulator.sh
```

You can also run the script path from another directory:

```bash
./jy_app/simulator/FloatairSimulator/develop-simulator.sh
```

The script:

1. Removes the old `build-macos-llvm`.
2. Reconfigures `cmake -S . -B simulator/FloatairSimulator/build-macos-llvm` with `-DCMAKE_C_COMPILER=clang`.
3. Builds `floatair_simulator`.
4. Keeps CMake/Ninja intermediate files for incremental builds and `compile_commands.json`.
5. By default, enters `build-macos-llvm` and launches `./floatair_simulator`. With `--no_run`, it only builds and installs.

## Manual Build and Run

If you want to build manually, run CMake from the repository root.

### Linux

Using `gcc`:

```bash
cmake -S . -B simulator/FloatairSimulator/build-linux-x86-gcc -G Ninja \
  -DCMAKE_C_COMPILER=gcc \
  -DARCH=x86 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-linux-x86-gcc
cd simulator/FloatairSimulator/build-linux-x86-gcc
./floatair_simulator
```

Using `clang`:

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

Native architecture:

```bash
cmake -S . -B simulator/FloatairSimulator/build-macos-llvm -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator/FloatairSimulator/build-macos-llvm
cd simulator/FloatairSimulator/build-macos-llvm
./floatair_simulator
```

For cross-architecture or universal builds, set `CMAKE_OSX_ARCHITECTURES` and make sure SDL2 includes the target architecture. Homebrew SDL2 is not recommended for this.

### MinGW

Using MinGW GCC x86:

```bash
cmake -S . -B simulator/FloatairSimulator/build-mingw-x86-gcc -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="<MINGW_BIN>/i686-w64-mingw32-gcc.exe"
cmake --build simulator/FloatairSimulator/build-mingw-x86-gcc
```

`<MINGW_BIN>` is the local MinGW `bin` directory. If it is already in `PATH`, a short compiler name can be used:

```bash
cmake -S . -B build-mingw-x86-gcc -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build-mingw-x86-gcc --target floatair_simulator

cmake -S . -B build-mingw-x86-llvm -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build-mingw-x86-llvm --target floatair_simulator
```

The `clang` example requires a Clang toolchain that can generate MinGW ABI artifacts. A normal MinGW GCC package usually does not provide it.

MinGW Clang is available from LLVM-MinGW releases: `https://github.com/mstorsjo/llvm-mingw/releases`.

### MSVC

Using `cl.exe`:

```bat
cmake -S . -B simulator\FloatairSimulator\build-msvc-x86-cl -G Ninja ^
  -DCMAKE_BUILD_TYPE=Debug
cmake --build simulator\FloatairSimulator\build-msvc-x86-cl
```

This uses the target bitness from the current developer command prompt. Prefer x86 Developer Command Prompt, or run:

```bat
VsDevCmd.bat -arch=x86 -host_arch=x64
```

Using Visual Studio LLVM `clang.exe` / `clang-cl.exe`:

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

`<VS_LLVM_BIN>` is the Visual Studio LLVM directory, such as `VC\Tools\Llvm\bin`, `VC\Tools\Llvm\x64\bin`, or `VC\Tools\Llvm\x86\bin`. For x64 LLVM builds, use `x86_64-pc-windows-msvc`.

Manual run notes:

1. Start from the corresponding `build-*` directory so `simulator_socket.conf` can be read.
2. Do not delete the generated `jyt_d/` directory.

## Install to a Standalone Directory

The one-click scripts run `ninja install` after building and install executable files and resources into the project root `install/` directory. Use `--prefix` to override the default install directory.

Linux example:

```bash
./simulator/FloatairSimulator/develop-simulator.sh \
  --arch x86 \
  --no_run
```

Windows MinGW example:

```bat
simulator\FloatairSimulator\develop-simulator-mingw.bat --arch x86 --no_run
```

Windows MSVC example:

```bat
simulator\FloatairSimulator\develop-simulator-msvc.bat --arch x86 --no_run
```

Manual CMake install:

```bash
cmake -S . -B simulator/FloatairSimulator/build-linux-x86-gcc -G Ninja \
  -DCMAKE_C_COMPILER=gcc \
  -DARCH=x86 \
  -DCMAKE_INSTALL_PREFIX=install/linux-x86-gcc
ninja -C simulator/FloatairSimulator/build-linux-x86-gcc
ninja -C simulator/FloatairSimulator/build-linux-x86-gcc install
```

The install directory contains:

- `floatair_simulator` / `floatair_simulator.exe`
- `simulator_socket.conf`
- `simulator_event_panel.py`
- `jyt_d/`
- `romfs/`
- On Windows, `SDL2.dll` and `libwinpthread-1.dll`

## OS Event Panel

`simulator_event_panel.py` is a `tkinter` helper tool. It injects system events through the simulator's FIFO / Windows named pipe.

Built-in operations:

- Host connect / disconnect
- Low battery, charging, and SOC adjustment
- Wear / remove
- Single click, double click, triple click, long press, and swipe
- IMU click and head-up / head-down
- Time sync
- Incoming call ring, answer, and hang up

Python dependency notes:

- No third-party `pip` dependency is required.
- The current Python environment must include `tkinter`.

Check Tk support:

```bash
cd simulator/FloatairSimulator
python3 -m tkinter
```

If `python3 -m tkinter` cannot start, install Tk support:

```bash
# Debian / Ubuntu
sudo apt install python3-tk

# Fedora
sudo dnf install python3-tkinter
```

On Windows, the official python.org installer is usually enough. Make sure `tcl/tk and IDLE` is selected, then check:

```bat
py -m tkinter
```

On macOS:

```bash
# The official python.org package usually includes Tk
python3 -m tkinter

# For Homebrew Python, install the Tk package matching the Python version
brew search python-tk
brew install python-tk@<PYTHON_VERSION>
```

Start the panel:

```bash
cd simulator/FloatairSimulator
python3 simulator_event_panel.py
```

Notes:

1. Startup order is flexible. The panel can be started before or after `floatair_simulator`.
2. Event sending works only after the simulator has started and created the FIFO / named pipe. Otherwise, the panel reports that the FIFO / named pipe does not exist.
3. Linux / macOS default path: `/tmp/floatair_sim_event_fifo`.
4. Windows default path: `\\.\pipe\floatair_sim_event_fifo`.
5. The channel path is fixed by the simulator. The event panel does not provide a custom path option.

## Artifacts

The scripts do not generate a single-file packaged simulator by default. They directly build and run the native executable under the build directory:

- Linux: `simulator/FloatairSimulator/build-linux-x86-gcc/floatair_simulator`
- macOS: `simulator/FloatairSimulator/build-macos-llvm/floatair_simulator`
- MinGW: `simulator/FloatairSimulator/build-mingw-x86-gcc/floatair_simulator.exe`
- MSVC: `simulator/FloatairSimulator/build-msvc-x86-cl/floatair_simulator.exe`

For manual builds, artifacts are placed under the directory specified by CMake `-B`, such as `build-linux-x86-gcc` or `build-mingw-x86-llvm`.

After a successful build, the run directory usually contains at least:

- `floatair_simulator` / `floatair_simulator.exe`
- `simulator_socket.conf`
- `jyt_d/`
- On Windows, `SDL2.dll`
- On Windows, other runtime DLLs may also be present
