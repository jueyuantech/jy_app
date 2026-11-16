#!/usr/bin/env python3
"""计算字体文件 MD5，并将摘要写入同目录的 font.md5。

支持传入多个字体文件（多字号点阵字库场景），
将所有文件内容按顺序拼接后计算 MD5，确保任一字库变更都会使摘要变化。
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def calculate_md5(paths: list[Path]) -> str:
    """分块计算多个文件拼接后的 MD5，避免将整个字库加载到内存。"""
    digest = hashlib.md5()
    for path in paths:
        with path.open("rb") as font_file:
            for chunk in iter(lambda: font_file.read(1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    """解析命令行参数。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--font",
        required=True,
        type=Path,
        action="append",
        help="待计算的字体文件路径，可多次指定（多字号字库）",
    )
    return parser.parse_args()


def main() -> int:
    """生成 font.md5 文件（与第一个字体文件同目录）。"""
    args = parse_args()
    font_paths = [p.resolve() for p in args.font]
    for font_path in font_paths:
        if not font_path.is_file():
            raise FileNotFoundError(f"font file not found: {font_path}")

    output_path = font_paths[0].with_name("font.md5")
    temporary_path = output_path.with_name(f"{output_path.name}.tmp")
    temporary_path.write_text(calculate_md5(font_paths), encoding="ascii")
    temporary_path.replace(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
