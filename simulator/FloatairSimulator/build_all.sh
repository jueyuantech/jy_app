#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/../.."
DEVELOP_SCRIPT="$SCRIPT_DIR/develop-simulator.sh"
DEVELOP_CMD=(bash "$DEVELOP_SCRIPT")
DRY_RUN=0
PRODUCT_NAME=""
OS_SDK_ARCHIVE=""
PROMPTED_PRODUCT=0
BUILD_COUNT=0
FAIL_COUNT=0
NEEDS_SEPARATOR=0
FAILED_BUILDS=()

if [ -t 1 ]; then
    COLOR_GREEN="$(printf '\033[32m')"
    COLOR_RED="$(printf '\033[31m')"
    COLOR_RESET="$(printf '\033[0m')"
else
    COLOR_GREEN=""
    COLOR_RED=""
    COLOR_RESET=""
fi

status_yes() {
    printf '%s%s%s' "$COLOR_GREEN" "yes" "$COLOR_RESET"
}

status_no() {
    printf '%s%s%s' "$COLOR_RED" "no" "$COLOR_RESET"
}

status_success() {
    printf '%s[SUCCESS]%s %s\n' "$COLOR_GREEN" "$COLOR_RESET" "$1"
}

status_error() {
    printf '%s[ERROR]%s %s\n' "$COLOR_RED" "$COLOR_RESET" "$1"
}

usage() {
    echo "Usage: $(basename "$0") [OS_SDK_ARCHIVE] [--dry-run] [--product PRODUCT]"
}

select_product() {
    local products=()
    local dir
    local idx
    local choice

    for dir in "$PROJECT_ROOT"/products/*; do
        [ -d "$dir" ] || continue
        products+=("$(basename "$dir")")
    done

    if [ "${#products[@]}" -eq 0 ]; then
        status_error "No products found in products directory."
        exit 1
    fi

    echo
    echo "Select product:"
    for idx in "${!products[@]}"; do
        printf "  %d. %s\n" "$((idx + 1))" "${products[$idx]}"
    done

    while true; do
        read -r -p "Enter product number: " choice
        if [[ "$choice" =~ ^[0-9]+$ ]] &&
                (( choice >= 1 && choice <= ${#products[@]} )); then
            PRODUCT_NAME="${products[$((choice - 1))]}"
            return
        fi
        status_error "Invalid product selection."
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
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --product)
            if [ "$#" -lt 2 ]; then
                echo "[ERROR] --product requires a value."
                usage
                exit 1
            fi
            PRODUCT_NAME="$2"
            shift 2
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
                echo "[ERROR] Unknown argument: $1"
                usage
                exit 1
            fi
            ;;
    esac
done

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

print_command() {
    printf '[DRY-RUN]'
    while [ "$#" -gt 0 ]; do
        printf ' %q' "$1"
        shift
    done
    printf '\n'
}

run_build() {
    local label="$1"
    shift

    BUILD_COUNT=$((BUILD_COUNT + 1))
    if [ "$DRY_RUN" = "1" ]; then
        print_command "$@"
    else
        echo "[BUILD] $label"
        if "$@"; then
            :
        else
            FAIL_COUNT=$((FAIL_COUNT + 1))
            FAILED_BUILDS+=("$label")
            status_error "Build failed: $label"
        fi
    fi
    NEEDS_SEPARATOR=1
}

skip_build() {
    echo "[SKIP] $1: $2"
    NEEDS_SEPARATOR=1
}

print_separator_if_needed() {
    if [ "$NEEDS_SEPARATOR" = "1" ]; then
        echo
        NEEDS_SEPARATOR=0
    fi
}

run_linux_local_builds() {
    local arch
    local compiler_kind

    for arch in x86 x64; do
        for compiler_kind in gcc llvm; do
            print_separator_if_needed
            if "${DEVELOP_CMD[@]}" --platform linux --compiler "$compiler_kind" --arch "$arch" \
                    --check-platform-deps >/dev/null 2>&1; then
                echo "[CHECK] Linux $arch $compiler_kind: $(status_yes)"
                run_build "Linux $arch $compiler_kind" \
                    "${DEVELOP_CMD[@]}" ${OS_SDK_ARCHIVE:+"$OS_SDK_ARCHIVE"} --platform linux --compiler "$compiler_kind" --arch "$arch" \
                    --prefix "install/linux-$arch-$compiler_kind" --product "$PRODUCT_NAME" --no_run
            else
                echo "[CHECK] Linux $arch $compiler_kind: $(status_no)"
                skip_build "Linux $arch $compiler_kind" "platform dependencies are not available"
            fi
        done
    done
}

run_mingw_builds() {
    local arch
    local compiler_kind

    for arch in x86 x64; do
        for compiler_kind in gcc llvm; do
            print_separator_if_needed
            if "${DEVELOP_CMD[@]}" --platform mingw --compiler "$compiler_kind" --arch "$arch" \
                    --check-platform-deps >/dev/null 2>&1; then
                echo "[CHECK] MinGW $arch $compiler_kind: $(status_yes)"
                run_build "Windows MinGW $arch $compiler_kind" \
                    "${DEVELOP_CMD[@]}" ${OS_SDK_ARCHIVE:+"$OS_SDK_ARCHIVE"} --platform mingw --compiler "$compiler_kind" --arch "$arch" \
                    --prefix "install/mingw-$arch-$compiler_kind" --product "$PRODUCT_NAME" --no_run
            else
                echo "[CHECK] MinGW $arch $compiler_kind: $(status_no)"
                skip_build "Windows MinGW $arch $compiler_kind" "platform dependencies are not available"
            fi
        done
    done
}

run_macos_local_build() {
    print_separator_if_needed
    if "${DEVELOP_CMD[@]}" --platform macos --compiler llvm --check-platform-deps >/dev/null 2>&1; then
        echo "[CHECK] macOS llvm: $(status_yes)"
        run_build "macOS llvm" \
            "${DEVELOP_CMD[@]}" ${OS_SDK_ARCHIVE:+"$OS_SDK_ARCHIVE"} --platform macos --compiler llvm --prefix "install/macos-llvm" \
            --product "$PRODUCT_NAME" --no_run
    else
        echo "[CHECK] macOS llvm: $(status_no)"
        skip_build "macOS llvm" "platform dependencies are not available"
    fi
}

case "$(uname -s)" in
    Linux)
        echo "[INFO] Detecting supported Linux host builds..."
        run_linux_local_builds
        run_mingw_builds
        ;;
    Darwin)
        echo "[INFO] Detecting supported macOS host builds..."
        run_macos_local_build
        run_mingw_builds
        ;;
    *)
        status_error "Unsupported host: $(uname -s)"
        exit 1
        ;;
esac

if [ "$BUILD_COUNT" = "0" ]; then
    status_error "No supported simulator build was detected."
    exit 1
fi

print_separator_if_needed
if [ "$FAIL_COUNT" -gt 0 ]; then
    status_error "Some simulator builds failed. Failed count: $FAIL_COUNT / $BUILD_COUNT"
    for failed_build in "${FAILED_BUILDS[@]}"; do
        echo "[FAILED] $failed_build"
    done
    exit 1
fi

if [ "$DRY_RUN" = "1" ]; then
    status_success "Dry run finished. Supported build count: $BUILD_COUNT"
else
    status_success "All supported simulator builds finished. Build count: $BUILD_COUNT"
fi
