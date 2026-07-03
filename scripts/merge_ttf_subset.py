# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import shutil
import struct
import uuid
from pathlib import Path

from fontTools import subset
from fontTools.merge import Merger
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable
from fontTools.varLib.instancer import instantiateVariableFont


DROP_LAYOUT_TABLES = ["GDEF", "GPOS", "GSUB", "kern"]
DROP_ALWAYS_TABLES = ["vhea", "vmtx"]

COMPLEX_LAYOUT_RANGES = [
    (0x0590, 0x05FF),   # Hebrew
    (0x0600, 0x06FF),   # Arabic
    (0x0700, 0x074F),   # Syriac
    (0x0750, 0x077F),   # Arabic Supplement
    (0x0780, 0x07BF),   # Thaana
    (0x07C0, 0x07FF),   # NKo
    (0x0870, 0x089F),   # Arabic Extended-B
    (0x08A0, 0x08FF),   # Arabic Extended-A
    (0x0900, 0x097F),   # Devanagari
    (0x0980, 0x09FF),   # Bengali
    (0x0A00, 0x0A7F),   # Gurmukhi
    (0x0A80, 0x0AFF),   # Gujarati
    (0x0B00, 0x0B7F),   # Oriya
    (0x0B80, 0x0BFF),   # Tamil
    (0x0C00, 0x0C7F),   # Telugu
    (0x0C80, 0x0CFF),   # Kannada
    (0x0D00, 0x0D7F),   # Malayalam
    (0x0D80, 0x0DFF),   # Sinhala
    (0x0E00, 0x0E7F),   # Thai
    (0x0E80, 0x0EFF),   # Lao
    (0x0F00, 0x0FFF),   # Tibetan
    (0x1000, 0x109F),   # Myanmar
    (0x1780, 0x17FF),   # Khmer
    (0x1800, 0x18AF),   # Mongolian
    (0xFB50, 0xFDFF),   # Arabic Presentation Forms-A
    (0xFE70, 0xFEFF),   # Arabic Presentation Forms-B
]


def collect_cmap_unicodes(font_path: Path) -> set[int]:
    """收集字体 cmap 中声明支持的 Unicode 码点。"""

    with TTFont(font_path, lazy=True) as font:
        cmap = font.getBestCmap()
        if not cmap:
            return set()
        return set(cmap.keys())


def uses_complex_layout(unicodes: set[int]) -> bool:
    """判断字符集是否包含依赖 OpenType layout 的复杂排版语系。"""

    for codepoint in unicodes:
        for start, end in COMPLEX_LAYOUT_RANGES:
            if start <= codepoint <= end:
                return True
    return False


def subset_font(input_path: Path, output_path: Path, unicodes: set[int], keep_layout: bool) -> None:
    """按指定 Unicode 集裁剪字体，并按语系决定是否保留 OpenType layout 表。"""

    options = subset.Options()
    options.drop_tables += DROP_ALWAYS_TABLES
    if keep_layout:
        options.layout_closure = False
        options.layout_features = ["*"]
    else:
        options.drop_tables += DROP_LAYOUT_TABLES
        options.layout_features = []
    options.name_IDs = ["*"]
    options.name_legacy = True
    options.name_languages = ["*"]
    options.notdef_glyph = True
    options.recalc_bounds = True
    options.recalc_timestamp = False
    options.recommended_glyphs = True

    font = load_static_font(input_path)
    font_subsetter = subset.Subsetter(options=options)
    font_subsetter.populate(unicodes=unicodes)
    font_subsetter.subset(font)
    normalize_cmap_tables(font)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    font.save(output_path)
    font.close()


def load_static_font(input_path: Path) -> TTFont:
    """加载字体；如果是 variable font，则按默认轴值实例化成静态字体。"""

    font = TTFont(input_path)
    if "fvar" not in font:
        return font

    axis_limits = {
        axis.axisTag: axis.defaultValue
        for axis in font["fvar"].axes
    }
    instantiateVariableFont(font, axis_limits, inplace=True, optimize=True)
    return font


def normalize_cmap_tables(font: TTFont) -> None:
    """移除保存时会溢出的 cmap format 4，并保留完整 format 12 映射。"""

    if "cmap" not in font:
        return

    cmap_table = font["cmap"]
    unicode_cmap: dict[int, str] = {}
    for table in cmap_table.tables:
        if table.isUnicode():
            unicode_cmap.update(table.cmap)

    format12_table = next(
        (
            table
            for table in cmap_table.tables
            if table.format == 12 and table.platformID == 3 and table.platEncID == 10
        ),
        None,
    )
    if unicode_cmap and format12_table is None:
        format12_table = CmapSubtable.newSubtable(12)
        format12_table.platformID = 3
        format12_table.platEncID = 10
        format12_table.language = 0
        cmap_table.tables.append(format12_table)
    if unicode_cmap and format12_table is not None:
        format12_table.cmap = dict(unicode_cmap)

    normalized_tables = []
    dropped_format4_count = 0
    for table in cmap_table.tables:
        if table.format == 4:
            try:
                table.compile(font)
            except (struct.error, OverflowError):
                dropped_format4_count += 1
                continue
        normalized_tables.append(table)
    cmap_table.tables = normalized_tables

    if dropped_format4_count:
        print(f"dropped oversized cmap format 4 table count: {dropped_format4_count}")


def parse_fallback_paths(value: str) -> list[Path]:
    """解析逗号分隔的补充字体路径列表。"""

    paths = [Path(item.strip()) for item in value.split(",") if item.strip()]
    if not paths:
        raise ValueError("fallback font list is empty")
    return paths


def merge_ttf_subset(base_path: Path, fallback_paths: list[Path], output_path: Path) -> None:
    """把补充字体中主字体缺失的字符合并到主字体并输出裁剪后的 TTF。"""

    if not base_path.is_file():
        raise FileNotFoundError(f"base font not found: {base_path}")
    for fallback_path in fallback_paths:
        if not fallback_path.is_file():
            raise FileNotFoundError(f"fallback font not found: {fallback_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    base_unicodes = collect_cmap_unicodes(base_path)
    output_unicodes = set(base_unicodes)
    fallback_jobs: list[tuple[Path, set[int]]] = []

    for fallback_path in fallback_paths:
        fallback_unicodes = collect_cmap_unicodes(fallback_path)
        missing_unicodes = fallback_unicodes - output_unicodes
        if missing_unicodes:
            fallback_jobs.append((fallback_path, missing_unicodes))
            output_unicodes.update(missing_unicodes)
        print(
            f"fallback: {fallback_path} unicode={len(fallback_unicodes)} "
            f"missing={len(missing_unicodes)}"
        )

    if not fallback_jobs:
        shutil.copyfile(base_path, output_path)
        print("fallback has no missing Unicode codepoints; copied base font")
        return

    tmp_root = output_path.parent / f".merge_ttf_subset_{uuid.uuid4().hex}"
    try:
        tmp_root.mkdir(parents=True, exist_ok=False)
        base_subset_path = tmp_root / "base.subset.ttf"
        merged_path = tmp_root / "merged.ttf"
        merge_inputs = [str(base_subset_path)]
        keep_layout = uses_complex_layout(output_unicodes)

        subset_font(base_path, base_subset_path, base_unicodes, keep_layout)
        for index, (fallback_path, missing_unicodes) in enumerate(fallback_jobs, start=1):
            fallback_subset_path = tmp_root / f"fallback_{index}.subset.ttf"
            subset_font(fallback_path, fallback_subset_path, missing_unicodes, keep_layout)
            merge_inputs.append(str(fallback_subset_path))

        merger = Merger()
        merged_font = merger.merge(merge_inputs)
        normalize_cmap_tables(merged_font)
        merged_font.save(merged_path)
        merged_font.close()

        subset_font(merged_path, output_path, output_unicodes, keep_layout)
    finally:
        shutil.rmtree(tmp_root, ignore_errors=True)

    print(f"base unicode count: {len(base_unicodes)}")
    print(f"merged fallback font count: {len(fallback_jobs)}")
    print(f"output unicode count: {len(output_unicodes)}")
    print(f"keep OpenType layout tables: {uses_complex_layout(output_unicodes)}")
    print(f"output: {output_path}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Merge missing Unicode glyphs from a fallback TTF into a base TTF."
    )
    parser.add_argument("base_arg", nargs="?", help="主字体 TTF 路径")
    parser.add_argument("fallback_arg", nargs="?", help="补充字体 TTF 路径；多个路径用英文逗号分隔")
    parser.add_argument("output_arg", nargs="?", help="输出 TTF 路径")
    parser.add_argument("--base", dest="base_opt", help="主字体 TTF 路径")
    parser.add_argument("--fallback", dest="fallback_opt", help="补充字体 TTF 路径；多个路径用英文逗号分隔")
    parser.add_argument("--output", dest="output_opt", help="输出 TTF 路径")
    args = parser.parse_args()

    base = args.base_opt or args.base_arg
    fallback = args.fallback_opt or args.fallback_arg
    output = args.output_opt or args.output_arg
    if not base or not fallback or not output:
        parser.error("base, fallback and output are required")

    merge_ttf_subset(Path(base), parse_fallback_paths(fallback), Path(output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
