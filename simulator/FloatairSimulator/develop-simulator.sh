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
    echo "Usage: $(basename "$0") [OS_SDK_ARCHIVE] [--platform linux|macos] [--arch x86|x64] [--compiler gcc|llvm] [--build-dir DIR] [--prefix DIR] [--product PRODUCT] [--no_run] [--check-platform-deps]"
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
        *) die "Unsupported host: $HOST_OS." ;;
    esac
fi

case "$PLATFORM" in
    linux|Linux) PLATFORM="linux" ;;
    macos|mac|darwin|Darwin) PLATFORM="macos" ;;
    *) die "Unsupported platform: $PLATFORM. Use linux or macos." ;;
esac

if [ "$PLATFORM" = "macos" ]; then
    [ "$HOST_OS" = "Darwin" ] || die "macOS builds require a Darwin host."
    has clang || die "Compiler not found: clang"
    # macOS 使用 Clang 默认的本机目标，日志架构必须与实际编译目标一致。
    MACOS_TARGET="$(clang -dumpmachine)" || die "Cannot determine the macOS compiler target."
    ARCH="${MACOS_TARGET%%-*}"
else
    case "$ARCH" in
        x86|i386|i686|x86_32|32)
            ARCH="x86"
            CMAKE_ARCH="x86"
            ;;
        x64|amd64|x86_64|64)
            ARCH="x64"
            CMAKE_ARCH="amd64"
            ;;
        *) die "Unsupported arch: $ARCH. Use x86 or x64." ;;
    esac
fi

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

# C/C++ 必须使用同一工具链，供依赖探测与 CMake 配置共用。
prepare_compilers() {
    case "$PLATFORM:$COMPILER_KIND" in
        linux:gcc) CC_COMPILER=gcc; CXX_COMPILER=g++ ;;
        linux:llvm|macos:llvm) CC_COMPILER=clang; CXX_COMPILER=clang++ ;;
    esac
    has "$CC_COMPILER" && has "$CXX_COMPILER"
}

# 同时编译并链接 C 与使用标准库的 C++，确认本机工具链可用。
compilers_can_link() {
    local tmp_root rc=0 arch_args=()
    [ "$PLATFORM" = "linux" ] && [ "$ARCH" = "x86" ] && arch_args=(-m32)
    tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/floatair-compilers.XXXXXX")"
    printf '#include <stdio.h>\nint main(void){return puts("probe") < 0;}\n' > "$tmp_root/probe.c"
    printf '#include <string>\nint main(){std::string s("probe"); return s.empty();}\n' > "$tmp_root/probe.cc"
    if ! "$CC_COMPILER" "${arch_args[@]}" \
            "$tmp_root/probe.c" -o "$tmp_root/probe-c" >/dev/null 2>&1 ||
       ! "$CXX_COMPILER" "${arch_args[@]}" \
            "$tmp_root/probe.cc" -o "$tmp_root/probe-cxx" >/dev/null 2>&1; then
        rc=1
    fi
    rm -rf "$tmp_root"
    return "$rc"
}

check_deps() {
    local failed=0
    echo "[INFO] Checking $PLATFORM deps: arch=$ARCH compiler=$COMPILER_KIND"

    has cmake || { echo "[CHECK] cmake: no"; failed=1; }
    has ninja || { echo "[CHECK] ninja: no"; failed=1; }

    [ -f "$PROJECT_ROOT/thirdparty/SDL2/CMakeLists.txt" ] || { echo "[CHECK] SDL2 source: no"; failed=1; }

    if prepare_compilers; then
        compilers_can_link || { echo "[CHECK] C/C++ compiler link: no ($CC_COMPILER / $CXX_COMPILER)"; failed=1; }
    else
        echo "[CHECK] C/C++ compiler pair: no ($CC_COMPILER / $CXX_COMPILER)"
        failed=1
    fi

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

    echo "-DCMAKE_C_COMPILER=$CC_COMPILER"
    echo "-DCMAKE_CXX_COMPILER=$CXX_COMPILER"
    if [ "$PLATFORM" = "linux" ]; then
        echo "-DARCH=$CMAKE_ARCH"
    fi
}

collect_cmake_args() {
    CMAKE_ARGS=()
    local arg
    while IFS= read -r arg; do
        CMAKE_ARGS+=("$arg")
    done < <(cmake_args)
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

SIMULATOR_EXE="$BUILD_DIR_ABS/floatair_simulator"
[ -f "$SIMULATOR_EXE" ] || die "Simulator executable not found: \"$SIMULATOR_EXE\""
ok "$PLATFORM simulator build finished."

echo "[INFO] Installing $PLATFORM simulator to \"$INSTALL_PREFIX\"..."
ninja -C "$BUILD_DIR" install

if [ "$NO_RUN" = "1" ]; then
    echo "[INFO] --no_run specified; skipping simulator launch."
    echo "[INFO] Output: \"$SIMULATOR_EXE\""
else
    echo "[INFO] Starting: \"$SIMULATOR_EXE\""
    cd "$BUILD_DIR_ABS"
    exec "./floatair_simulator"
fi
