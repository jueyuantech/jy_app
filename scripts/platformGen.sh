#!/usr/bin/env bash
set -euo pipefail
if [ $# -ne 1 ]; then
  echo "Usage: $0 <input_dir_path>" >&2
  exit 1
fi
in="$1"
if [ ! -d "$in" ]; then
  echo "Input directory not found: $in" >&2
  exit 1
fi
script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(dirname "$script_dir")"
src1="$in/apps/jyt/system_manager/elf_common.h"
dest1="common/elf_common.h"
if [ ! -f "$src1" ]; then
  echo "Source not found: $src1" >&2
  exit 1
fi
mkdir -p "$(dirname "$dest1")"
cp "$src1" "$dest1"

elfloader_c="$in/apps/jyt/system_manager/elfloader.c"
if [ ! -f "$elfloader_c" ]; then
  echo "Source not found: $elfloader_c" >&2
  exit 1
fi
"$repo_root/scripts/SymbolTableGen.sh" "$elfloader_c"
echo "Generated $dest1"
