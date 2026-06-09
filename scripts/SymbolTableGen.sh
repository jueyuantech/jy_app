#!/bin/bash
set -e
infile="$1"
if [ -z "$infile" ]; then
  echo "Usage: $0 <file-to-parse>" >&2
  exit 1
fi
if [ ! -f "$infile" ]; then
  echo "Error: file not found: $infile" >&2
  exit 1
fi
script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(dirname "$script_dir")"
out="$repo_root/bes28/SymbolTable.def"
awk '
BEGIN { in_block = 0 }
{
  line = $0
  if (line ~ /^[[:space:]]*#define[[:space:]]+ADDSYM/) {
    if (in_block && index(line, "*/") > 0) in_block = 0
    if (!in_block) {
      pos_start = index(line, "/*")
      if (pos_start > 0) {
        pos_endb = index(line, "*/")
        if (!(pos_endb > 0 && pos_endb > pos_start)) in_block = 1
      }
    }
    next
  }
  pos_add = match(line, /ADDSYM[[:space:]]*\([[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\)/)
  if (pos_add > 0) {
    sym = substr(line, RSTART, RLENGTH)
    gsub(/^.*\(/, "", sym)
    gsub(/\).*$/, "", sym)
    gsub(/^[[:space:]]+|[[:space:]]+$/, "", sym)
    comment = "none"
    if (in_block) {
      pos_end = index(line, "*/")
      if (pos_end > 0 && pos_end < pos_add) { comment = "none" } else { comment = "block" }
    } else {
      pos_line = index(line, "//")
      if (pos_line > 0 && pos_line < pos_add) {
        comment = "line"
      } else {
        pos_start = index(line, "/*")
        if (pos_start > 0 && pos_start < pos_add) {
          pos_endb = index(line, "*/")
          if (pos_endb > 0 && pos_endb > pos_start && pos_endb < pos_add) { comment = "none" } else { comment = "block" }
        }
      }
    }
    if (comment == "line") { print "// " sym }
    else if (comment == "block") { print "/* " sym " */" }
    else { print sym }
  }

  pos_pair = match(line, /\{[[:space:]]*"[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*"[[:space:]]*,/)
  if (pos_pair > 0) {
    tmp = substr(line, RSTART, RLENGTH)
    q1 = index(tmp, "\"")
    tmp2 = substr(tmp, q1 + 1)
    q2 = index(tmp2, "\"")
    sym = substr(tmp2, 1, q2 - 1)
    gsub(/^[[:space:]]+|[[:space:]]+$/, "", sym)
    comment = "none"
    if (in_block) {
      pos_end = index(line, "*/")
      if (pos_end > 0 && pos_end < pos_pair) { comment = "none" } else { comment = "block" }
    } else {
      pos_line = index(line, "//")
      if (pos_line > 0 && pos_line < pos_pair) {
        comment = "line"
      } else {
        pos_start = index(line, "/*")
        if (pos_start > 0 && pos_start < pos_pair) {
          pos_endb = index(line, "*/")
          if (pos_endb > 0 && pos_endb > pos_start && pos_endb < pos_pair) { comment = "none" } else { comment = "block" }
        }
      }
    }
    if (comment == "line") { print "// " sym }
    else if (comment == "block") { print "/* " sym " */" }
    else { print sym }
  }
  if (in_block) {
    if (index(line, "*/") > 0) in_block = 0
  } else {
    pos_start = index(line, "/*")
    if (pos_start > 0) {
      pos_endb = index(line, "*/")
      if (!(pos_endb > 0 && pos_endb > pos_start)) in_block = 1
    }
  }
}
' "$infile" > "$out"
# 检测重复：仅对有效清单（非注释行）进行重复检查；注释与有效交叉不计为重复
active_syms="$(sed -n 's/^[[:space:]]*\([A-Za-z_][A-Za-z0-9_]*\)[[:space:]]*$/\1/p' "$out" | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' || true)"
comment_syms_1="$(sed -n 's/^[[:space:]]*\/\/[[:space:]]*\([A-Za-z_][A-Za-z0-9_]*\)[[:space:]]*$/\1/p' "$out" || true)"
comment_syms_2="$(sed -n 's|^[[:space:]]*/\*[[:space:]]*\([A-Za-z_][A-Za-z0-9_]*\)[[:space:]]*\*/[[:space:]]*$|\1|p' "$out" || true)"
comment_syms="$(printf '%s\n%s\n' "$comment_syms_1" "$comment_syms_2" | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' || true)"
if [ -n "$active_syms" ]; then
  dups_active="$(echo "$active_syms" | sort | uniq -d || true)"
  if [ -n "$dups_active" ]; then
    echo "$dups_active" | while read -r sym; do
      [ -n "$sym" ] && echo "WARNING: duplicate symbol detected in active list: $sym" >&2
    done
  fi
fi
