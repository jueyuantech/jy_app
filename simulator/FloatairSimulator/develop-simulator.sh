#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/../.."
HOST_OS="$(uname -s)"

PLATFORM=""
ARCH="x86"
COMPILER_KIND=""
BUILD_DIR=""
INSTALL_PREFIX=""
PRODUCT_NAME=""
OS_SDK_ARCHIVE=""
PROMPTED_PRODUCT=0
NO_RUN=0
CHECK_PLATFORM_DEPS=0
LINUX_X86_SDL2_PATH="${LINUX_X86_SDL2_PATH:-/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig}"

if [ -t 1 ]; then
    GREEN="$(printf '\033[32m')"
    RED="$(printf '\033[31m')"
    RESET="$(printf '\033[0m')"
else
    GREEN=""
    RED=""
    RESET=""
fi

usage() {
    echo "Usage: $(basename "$0") [OS_SDK_ARCHIVE] [--platform linux|macos|mingw] [--arch x86|x64] [--compiler gcc|llvm] [--build-dir DIR] [--prefix DIR] [--product PRODUCT] [--no_run] [--check-platform-deps]"
    echo
    echo "Defaults: Linux host -> linux/gcc; macOS host -> macos/llvm (Apple clang)."
}

die() {
    echo "${RED}[ERROR]${RESET} $1"
    exit 1
}

ok() {
    echo "${GREEN}[SUCCESS]${RESET} $1"
}

need_value() {
    [ -n "${2:-}" ] || die "$1 requires a value."
}

has() {
    command -v "$1" >/dev/null 2>&1
}

abs_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$PROJECT_ROOT" "$1" ;;
    esac
}

select_product() {
    local products=()
    local dir
    local idx
    local choice

    for dir in products/*; do
        [ -d "$dir" ] || continue
        products+=("$(basename "$dir")")
    done

    if [ "${#products[@]}" -eq 0 ]; then
        die "未找到产品目录"
    fi

    echo
    echo "请选择产品:"
    for idx in "${!products[@]}"; do
        printf "  %d. %s\n" "$((idx + 1))" "${products[$idx]}"
    done

    while true; do
        read -r -p "请输入产品编号: " choice
        if [[ "$choice" =~ ^[0-9]+$ ]] &&
                (( choice >= 1 && choice <= ${#products[@]} )); then
            PRODUCT_NAME="${products[$((choice - 1))]}"
            return
        fi
        echo "无效的产品编号"
    done
}

select_os_sdk_archive() {
    local archive

    echo
    read -r -p "Enter OS SDK archive path (empty to use newest cache): " archive
    archive="${archive%\"}"
    archive="${archive#\"}"
    OS_SDK_ARCHIVE="$archive"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --platform)
            need_value "$1" "${2:-}"
            PLATFORM="$2"
            shift 2
            ;;
        --arch)
            need_value "$1" "${2:-}"
            ARCH="$2"
            shift 2
            ;;
        --compiler)
            need_value "$1" "${2:-}"
            COMPILER_KIND="$2"
            shift 2
            ;;
        --build-dir)
            need_value "$1" "${2:-}"
            BUILD_DIR="$2"
            shift 2
            ;;
        --prefix)
            need_value "$1" "${2:-}"
            INSTALL_PREFIX="$2"
            shift 2
            ;;
        --product)
            need_value "$1" "${2:-}"
            PRODUCT_NAME="$2"
            shift 2
            ;;
        --no_run|--no-run)
            NO_RUN=1
            shift
            ;;
        --check-platform-deps)
            CHECK_PLATFORM_DEPS=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            if [ -z "$OS_SDK_ARCHIVE" ] && [[ "$1" != -* ]]; then
                OS_SDK_ARCHIVE="$1"
                shift
            else
                usage
                die "Unknown argument: $1"
            fi
            ;;
    esac
done

if [ -z "$PLATFORM" ]; then
    case "$HOST_OS" in
        Linux) PLATFORM="linux" ;;
        Darwin) PLATFORM="macos" ;;
        *) die "Unsupported host: $HOST_OS. Use --platform mingw for cross builds." ;;
    esac
fi

case "$PLATFORM" in
    linux|Linux) PLATFORM="linux" ;;
    macos|mac|darwin|Darwin) PLATFORM="macos" ;;
    mingw|windows|windows-mingw) PLATFORM="mingw" ;;
    *) die "Unsupported platform: $PLATFORM. Use linux, macos, or mingw." ;;
esac

case "$ARCH" in
    x86|i386|i686|x86_32|32)
        ARCH="x86"
        CMAKE_ARCH="x86"
        MINGW_TRIPLE="i686-w64-mingw32"
        ;;
    x64|amd64|x86_64|64)
        ARCH="x64"
        CMAKE_ARCH="amd64"
        MINGW_TRIPLE="x86_64-w64-mingw32"
        ;;
    *) die "Unsupported arch: $ARCH. Use x86 or x64." ;;
esac

if [ -z "$COMPILER_KIND" ]; then
    if [ "$PLATFORM" = "macos" ]; then
        COMPILER_KIND="llvm"
    else
        COMPILER_KIND="gcc"
    fi
fi

case "$COMPILER_KIND" in
    gcc) ;;
    llvm|clang) COMPILER_KIND="llvm" ;;
    *) die "Unsupported compiler: $COMPILER_KIND. Use gcc or llvm." ;;
esac

if [ "$PLATFORM" = "macos" ] && [ "$COMPILER_KIND" != "llvm" ]; then
    die "macOS builds use llvm/clang."
fi

if [ "$PLATFORM" = "macos" ]; then
    BUILD_SUFFIX="macos-$COMPILER_KIND"
else
    BUILD_SUFFIX="$PLATFORM-$ARCH-$COMPILER_KIND"
fi

BUILD_DIR="${BUILD_DIR:-simulator/FloatairSimulator/build-$BUILD_SUFFIX}"
INSTALL_PREFIX="${INSTALL_PREFIX:-install/$BUILD_SUFFIX}"
BUILD_DIR_ABS="$(abs_path "$BUILD_DIR")"

linux_compiler() {
    if [ "$COMPILER_KIND" = "gcc" ]; then
        echo gcc
    else
        echo clang
    fi
}

compiler_can_link_linux() {
    local cc tmp_root out arch_args=()
    cc="$(linux_compiler)"
    has "$cc" || return 1
    [ "$ARCH" = "x86" ] && arch_args=(-m32)
    tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/floatair-linux-cc.XXXXXX")"
    printf 'int main(void){return 0;}\n' > "$tmp_root/probe.c"
    out="$tmp_root/probe"
    "$cc" "${arch_args[@]}" "$tmp_root/probe.c" -o "$out" >/dev/null 2>&1
    local rc=$?
    rm -rf "$tmp_root"
    return "$rc"
}

cmake_find_sdl2() {
    local tag="$1"
    shift
    local tmp_root rc
    tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/floatair-sdl2-$tag.XXXXXX")"
    printf '%s\n' \
        'cmake_minimum_required(VERSION 3.16)' \
        'project(sdl2_probe C)' \
        'find_package(SDL2 REQUIRED)' \
        > "$tmp_root/CMakeLists.txt"
    cmake -S "$tmp_root" -B "$tmp_root/build" -G Ninja "$@" >/dev/null 2>&1
    rc=$?
    rm -rf "$tmp_root"
    return "$rc"
}

linux_sdl2_available() {
    if [ "$ARCH" = "x86" ]; then
        has pkg-config && PKG_CONFIG_LIBDIR="$LINUX_X86_SDL2_PATH" pkg-config --exists sdl2
    else
        { has pkg-config && pkg-config --exists sdl2; } || { has cmake && cmake_find_sdl2 linux-x64; }
    fi
}

macos_sdl2_available() {
    local prefix cmake_prefix_path=""
    has cmake || return 1
    for prefix in "${SIMULATOR_SDL2_ROOT:-}" /opt/homebrew /usr/local; do
        if [ -n "$prefix" ] && [ -e "$prefix" ]; then
            cmake_prefix_path="${cmake_prefix_path:+$cmake_prefix_path;}$prefix"
        fi
    done
    if [ -n "$cmake_prefix_path" ]; then
        cmake_find_sdl2 macos "-DCMAKE_PREFIX_PATH=$cmake_prefix_path"
    else
        cmake_find_sdl2 macos
    fi
}

mingw_cc_args() {
    if [ "$COMPILER_KIND" = "gcc" ]; then
        has "$MINGW_TRIPLE-gcc" || return 1
        echo "-DCMAKE_C_COMPILER=$MINGW_TRIPLE-gcc"
    elif has "$MINGW_TRIPLE-clang"; then
        echo "-DCMAKE_C_COMPILER=$MINGW_TRIPLE-clang"
    elif [ "$HOST_OS" = "Linux" ] && has clang; then
        echo "-DCMAKE_C_COMPILER=clang"
        echo "-DCMAKE_C_COMPILER_TARGET=$MINGW_TRIPLE"
    else
        return 1
    fi
}

mingw_compiler_can_link() {
    local tmp_root out
    mingw_cc_args >/dev/null || return 1
    tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/floatair-mingw-cc.XXXXXX")"
    printf 'int main(void){return 0;}\n' > "$tmp_root/probe.c"
    out="$tmp_root/probe.exe"
    if [ "$COMPILER_KIND" = "gcc" ]; then
        "$MINGW_TRIPLE-gcc" "$tmp_root/probe.c" -o "$out" >/dev/null 2>&1
    elif has "$MINGW_TRIPLE-clang"; then
        "$MINGW_TRIPLE-clang" "$tmp_root/probe.c" -o "$out" >/dev/null 2>&1
    else
        clang -target "$MINGW_TRIPLE" "$tmp_root/probe.c" -o "$out" >/dev/null 2>&1
    fi
    local rc=$?
    rm -rf "$tmp_root"
    return "$rc"
}

mingw_sdl2_available() {
    [ -f "$SCRIPT_DIR/windows/SDL2/cmake/sdl2-config.cmake" ] &&
        [ -f "$SCRIPT_DIR/windows/SDL2/lib/mingw/$ARCH/SDL2.dll" ] &&
        [ -f "$SCRIPT_DIR/windows/SDL2/lib/mingw/$ARCH/libSDL2.dll.a" ]
}

check_deps() {
    local failed=0
    echo "[INFO] Checking $PLATFORM deps: arch=$ARCH compiler=$COMPILER_KIND"

    has cmake || { echo "[CHECK] cmake: no"; failed=1; }
    has ninja || { echo "[CHECK] ninja: no"; failed=1; }

    case "$PLATFORM" in
        linux)
            has "$(linux_compiler)" || { echo "[CHECK] compiler: no ($(linux_compiler))"; failed=1; }
            compiler_can_link_linux || { echo "[CHECK] compiler link: no"; failed=1; }
            linux_sdl2_available || { echo "[CHECK] SDL2: no"; failed=1; }
            ;;
        macos)
            [ "$HOST_OS" = "Darwin" ] || { echo "[CHECK] host: no ($HOST_OS)"; failed=1; }
            has clang || { echo "[CHECK] compiler: no (clang)"; failed=1; }
            macos_sdl2_available || { echo "[CHECK] SDL2: no"; failed=1; }
            ;;
        mingw)
            mingw_cc_args >/dev/null || { echo "[CHECK] compiler: no ($MINGW_TRIPLE-$COMPILER_KIND)"; failed=1; }
            mingw_compiler_can_link || { echo "[CHECK] compiler link: no"; failed=1; }
            mingw_sdl2_available || { echo "[CHECK] bundled SDL2: no"; failed=1; }
            ;;
    esac

    if [ "$failed" = "0" ]; then
        ok "$PLATFORM deps are available."
    else
        die "$PLATFORM deps are incomplete."
    fi
}

cmake_args() {
    echo "-DCMAKE_BUILD_TYPE=Debug"
    echo "-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
    echo "-DJY_APP_PRODUCT=$PRODUCT_NAME"
    [ -n "$OS_SDK_ARCHIVE" ] && echo "-DJY_APP_OS_SDK_ARCHIVE=$OS_SDK_ARCHIVE"

    case "$PLATFORM" in
        linux)
            echo "-DCMAKE_C_COMPILER=$(linux_compiler)"
            echo "-DARCH=$CMAKE_ARCH"
            [ "$ARCH" = "x86" ] && echo "-DLINUX_X86_SDL2_PATH=$LINUX_X86_SDL2_PATH"
            ;;
        macos)
            echo "-DCMAKE_C_COMPILER=clang"
            ;;
        mingw)
            mingw_cc_args
            ;;
    esac
}

collect_cmake_args() {
    CMAKE_ARGS=()
    local arg
    while IFS= read -r arg; do
        CMAKE_ARGS+=("$arg")
    done < <(cmake_args)
}

exe_name() {
    if [ "$PLATFORM" = "mingw" ]; then
        echo floatair_simulator.exe
    else
        echo floatair_simulator
    fi
}

cd "$PROJECT_ROOT"

if [ "$CHECK_PLATFORM_DEPS" = "1" ]; then
    check_deps
    exit 0
fi

if [ -z "$PRODUCT_NAME" ]; then
    select_product
    PROMPTED_PRODUCT=1
fi
echo "[INFO] Selected product: $PRODUCT_NAME"

if [ -z "$OS_SDK_ARCHIVE" ] && [ "$PROMPTED_PRODUCT" = "1" ]; then
    select_os_sdk_archive
fi

if [ -n "$OS_SDK_ARCHIVE" ]; then
    echo "[INFO] Using OS SDK archive: $OS_SDK_ARCHIVE"
else
    echo "[INFO] Using newest OS SDK cache."
fi

check_deps
collect_cmake_args

if [ -d "$BUILD_DIR" ]; then
    echo "[INFO] Removing build directory \"$BUILD_DIR\"..."
    rm -rf "$BUILD_DIR"
fi

echo "[INFO] Configuring $PLATFORM $ARCH $COMPILER_KIND in \"$BUILD_DIR\"..."
cmake -S . -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"

echo "[INFO] Building $PLATFORM simulator..."
ninja -C "$BUILD_DIR"

SIMULATOR_EXE="$BUILD_DIR_ABS/$(exe_name)"
[ -f "$SIMULATOR_EXE" ] || die "Simulator executable not found: \"$SIMULATOR_EXE\""
ok "$PLATFORM simulator build finished."

echo "[INFO] Installing $PLATFORM simulator to \"$INSTALL_PREFIX\"..."
ninja -C "$BUILD_DIR" install

if [ "$PLATFORM" = "mingw" ]; then
    echo "[INFO] This is a Windows executable. Run it on Windows, or use Wine if available."
elif [ "$NO_RUN" = "1" ]; then
    echo "[INFO] --no_run specified; skipping simulator launch."
    echo "[INFO] Output: \"$SIMULATOR_EXE\""
else
    echo "[INFO] Starting: \"$SIMULATOR_EXE\""
    cd "$BUILD_DIR_ABS"
    exec "./$(exe_name)"
fi
