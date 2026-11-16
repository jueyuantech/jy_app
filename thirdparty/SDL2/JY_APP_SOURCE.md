# SDL2 源码依赖

- 版本：2.32.10（release-2.32.10）
- 来源：https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10
- 源码归档：SDL2-2.32.10.tar.gz
- SHA-256：5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
- 许可证：zlib，见 LICENSE.txt。

Windows、macOS、Linux 模拟器通过 SDL 自带 CMake 编译并静态链接；ARM 固件不参与 SDL 构建。
上游源码保持原样；归档中的两个 Android 符号链接在 Windows 下展开为对应内容。
Linux 仍需目标架构的系统开发库（如 X11/Wayland）；静态链接 SDL 不代表静态链接所有系统库。
