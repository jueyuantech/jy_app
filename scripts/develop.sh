#!/bin/bash
set -euo pipefail

clean_build=0
product_name=""
os_sdk_archive=""
prompted_product=0

version_info=""
branch_name=""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$SCRIPT_DIR/.." || exit 1

select_product() {
    local products=()
    local dir
    local idx
    local choice

    for dir in products/*; do
        [ -d "$dir" ] || continue
        products+=("$(basename "$dir")")
    done

    if [[ ${#products[@]} -eq 0 ]]; then
        echo "未找到产品目录"
        exit 1
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
            product_name="${products[$((choice - 1))]}"
            return
        fi
        echo "无效的产品编号"
    done
}

select_os_sdk_archive() {
    local archive

    echo
    read -r -p "请输入 OS SDK 包路径（直接回车使用最新缓存）: " archive
    archive="${archive%\"}"
    archive="${archive#\"}"
    os_sdk_archive="$archive"
}

if ! command -v ninja >/dev/null 2>&1; then
    echo "未找到 ninja，请先安装 ninja"
    exit 1
fi

version_info=$(git describe --always --dirty --abbrev=8 2>/dev/null)
if [ $? -ne 0 ] || [ -z "$version_info" ]; then
    version_info="unknown"
fi

branch_name=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
if [ $? -ne 0 ] || [ -z "$branch_name" ]; then
    branch_name="unknown"
fi

echo "version_info: $version_info"
echo "branch_name: $branch_name"

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            clean_build=1
            shift
            ;;
        --product)
            if [[ $# -lt 2 ]]; then
                echo "--product 需要指定产品名"
                exit 1
            fi
            product_name="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OS_SDK_ARCHIVE] [--clean] [--product PRODUCT]"
            exit 0
            ;;
        *)
            if [[ -z "$os_sdk_archive" && "$1" != -* ]]; then
                os_sdk_archive="$1"
                shift
            else
                echo "未知参数: $1"
                echo "Usage: $0 [OS_SDK_ARCHIVE] [--clean] [--product PRODUCT]"
                exit 1
            fi
            ;;
    esac
done

if [[ -z "$product_name" ]]; then
    select_product
    prompted_product=1
fi

echo "product_name: $product_name"

if [[ -z "$os_sdk_archive" && $prompted_product -eq 1 ]]; then
    select_os_sdk_archive
fi

if [[ -n "$os_sdk_archive" ]]; then
    echo "os_sdk_archive: $os_sdk_archive"
else
    echo "os_sdk_archive: newest cache"
fi

if [[ $clean_build -eq 1 ]]; then
    rm -rf build
fi

mkdir -p build
cd build
cmake_args=(-G Ninja -DJY_APP_PRODUCT="$product_name")
if [[ -n "$os_sdk_archive" ]]; then
    cmake_args+=(-DJY_APP_OS_SDK_ARCHIVE="$os_sdk_archive")
fi
cmake "${cmake_args[@]}" ..
ninja
