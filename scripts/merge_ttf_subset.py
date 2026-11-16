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
from fontTools.ttLib.scaleUpem import scale_upem
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

AUTO_OVERRIDE_RANGES = [
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
    (0x19E0, 0x19FF),   # Khmer Symbols
    (0x2E80, 0x2EFF),   # CJK Radicals Supplement
    (0x2F00, 0x2FDF),   # Kangxi Radicals
    (0x3000, 0x303F),   # CJK Symbols and Punctuation
    (0x3040, 0x30FF),   # Hiragana and Katakana
    (0x3100, 0x312F),   # Bopomofo
    (0x31A0, 0x31BF),   # Bopomofo Extended
    (0x31C0, 0x31EF),   # CJK Strokes
    (0x31F0, 0x31FF),   # Katakana Phonetic Extensions
    (0x3400, 0x4DBF),   # CJK Unified Ideographs Extension A
    (0x4E00, 0x9FFF),   # CJK Unified Ideographs
    (0xA960, 0xA97F),   # Hangul Jamo Extended-A
    (0xAC00, 0xD7AF),   # Hangul Syllables
    (0xD7B0, 0xD7FF),   # Hangul Jamo Extended-B
    (0xF900, 0xFAFF),   # CJK Compatibility Ideographs
    (0xFB50, 0xFDFF),   # Arabic Presentation Forms-A
    (0xFE10, 0xFE1F),   # Vertical Forms
    (0xFE30, 0xFE4F),   # CJK Compatibility Forms
    (0xFE70, 0xFEFF),   # Arabic Presentation Forms-B
    (0xFF00, 0xFFEF),   # Halfwidth and Fullwidth Forms
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


def subset_font(input_path: Path,
                output_path: Path,
                unicodes: set[int],
                keep_layout: bool,
                target_units_per_em: int | None = None,
                layout_closure: bool = True) -> None:
    """按指定 Unicode 集裁剪字体，并按语系决定是否保留 OpenType layout 表。"""

    options = subset.Options()
    options.drop_tables += DROP_ALWAYS_TABLES
    if keep_layout:
        options.layout_closure = layout_closure
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
    normalize_units_per_em(font, target_units_per_em)
    font_subsetter = subset.Subsetter(options=options)
    font_subsetter.populate(unicodes=unicodes)
    font_subsetter.subset(font)
    normalize_cmap_tables(font)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    font.save(output_path)
    font.close()


def collect_units_per_em(font_path: Path) -> int:
    """读取字体 head.unitsPerEm。"""

    with TTFont(font_path, lazy=True) as font:
        return int(font["head"].unitsPerEm)


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


def normalize_units_per_em(font: TTFont, target_units_per_em: int | None) -> None:
    """把补充字体的 unitsPerEm 缩放到主字体一致。"""

    if target_units_per_em is None:
        return
    current_units_per_em = int(font["head"].unitsPerEm)
    if current_units_per_em == target_units_per_em:
        return
    scale_upem(font, target_units_per_em)


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


def parse_codepoint(value: str) -> int:
    """解析单个 Unicode 码点，支持 1780、0x1780、U+1780 写法。"""

    text = value.strip()
    if text.upper().startswith("U+"):
        text = "0x" + text[2:]
    codepoint = int(text, 16)
    if codepoint < 0 or codepoint > 0x10FFFF:
        raise ValueError(f"invalid Unicode codepoint: {value}")
    return codepoint


def parse_override_ranges(value: str | None) -> list[tuple[int, int]]:
    """解析逗号分隔的 Unicode 覆盖范围列表。"""

    if not value:
        return []

    ranges: list[tuple[int, int]] = []
    for item in value.split(","):
        text = item.strip()
        if not text:
            continue
        if "-" in text:
            start_text, end_text = text.split("-", 1)
            start = parse_codepoint(start_text)
            end = parse_codepoint(end_text)
        else:
            start = parse_codepoint(text)
            end = start
        if start > end:
            raise ValueError(f"invalid override range: {text}")
        ranges.append((start, end))
    return ranges


def filter_unicodes_by_ranges(unicodes: set[int], ranges: list[tuple[int, int]]) -> set[int]:
    """筛出落在指定 Unicode 范围内的码点。"""

    if not ranges:
        return set()

    return {
        codepoint
        for codepoint in unicodes
        if any(start <= codepoint <= end for start, end in ranges)
    }


def collect_auto_override_unicodes(unicodes: set[int]) -> set[int]:
    """按补充字体自身覆盖的语系范围，自动收集需要替换主字体的码点。"""

    return filter_unicodes_by_ranges(unicodes, AUTO_OVERRIDE_RANGES)


def merge_ttf_subset(
    base_path: Path,
    fallback_paths: list[Path],
    output_path: Path,
    override_ranges: list[tuple[int, int]] | None = None,
) -> None:
    """把补充字体中主字体缺失的字符合并到主字体并输出裁剪后的 TTF。"""

    if not base_path.is_file():
        raise FileNotFoundError(f"base font not found: {base_path}")
    for fallback_path in fallback_paths:
        if not fallback_path.is_file():
            raise FileNotFoundError(f"fallback font not found: {fallback_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    override_ranges = override_ranges or []
    base_units_per_em = collect_units_per_em(base_path)
    base_unicodes_original = collect_cmap_unicodes(base_path)
    fallback_unicodes_by_path: list[tuple[Path, set[int]]] = []
    auto_override_unicodes: set[int] = set()
    override_supplied_unicodes: set[int] = set()

    for fallback_path in fallback_paths:
        fallback_unicodes = collect_cmap_unicodes(fallback_path)
        fallback_unicodes_by_path.append((fallback_path, fallback_unicodes))
        auto_override_unicodes.update(collect_auto_override_unicodes(fallback_unicodes))
        override_supplied_unicodes.update(filter_unicodes_by_ranges(fallback_unicodes, override_ranges))

    override_supplied_unicodes.update(auto_override_unicodes)
    base_unicodes = base_unicodes_original - override_supplied_unicodes
    output_unicodes = set(base_unicodes)
    fallback_jobs: list[tuple[Path, set[int]]] = []

    for fallback_path, fallback_unicodes in fallback_unicodes_by_path:
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

        # 主字体保留全部原字形和排版表，只移除由补充字体接管的字符映射。
        # 按 Unicode 重新裁剪会删除没有独立编码的组合字形，破坏复杂文字排版。
        base_font = load_static_font(base_path)
        for table in base_font["cmap"].tables:
            if table.isUnicode():
                table.cmap = {cp: glyph for cp, glyph in table.cmap.items()
                              if cp not in override_supplied_unicodes}
        normalize_cmap_tables(base_font)
        base_font.recalcTimestamp = False
        base_font.save(base_subset_path)
        base_font.close()
        for index, (fallback_path, missing_unicodes) in enumerate(fallback_jobs, start=1):
            fallback_subset_path = tmp_root / f"fallback_{index}.subset.ttf"
            subset_font(fallback_path, fallback_subset_path, missing_unicodes, keep_layout, base_units_per_em, layout_closure=True)
            merge_inputs.append(str(fallback_subset_path))

        merger = Merger()
        merged_font = merger.merge(merge_inputs)
        # 新增组合字形不能改变原字体的命名、字号度量、行高和基线。
        with TTFont(base_path) as base_metrics:
            merged_font["name"] = base_metrics["name"]
            merged_font["OS/2"] = base_metrics["OS/2"]
            for field in ("ascent", "descent", "lineGap"):
                setattr(merged_font["hhea"], field, getattr(base_metrics["hhea"], field))
        merged_font.recalcTimestamp = False
        normalize_cmap_tables(merged_font)
        merged_font.save(output_path)
        merged_font.close()
    finally:
        shutil.rmtree(tmp_root, ignore_errors=True)

    print(f"base unicode count: {len(base_unicodes_original)}")
    print(f"base unitsPerEm: {base_units_per_em}")
    print(f"auto override unicode count: {len(auto_override_unicodes)}")
    print(f"override unicode count: {len(override_supplied_unicodes)}")
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
    parser.add_argument(
        "--override-ranges",
        dest="override_ranges",
        help="强制用补充字体覆盖主字体已有字形的 Unicode 范围，例如 1780-17FF,U+19E0-U+19FF",
    )
    args = parser.parse_args()

    base = args.base_opt or args.base_arg
    fallback = args.fallback_opt or args.fallback_arg
    output = args.output_opt or args.output_arg
    if not base or not fallback or not output:
        parser.error("base, fallback and output are required")

    merge_ttf_subset(
        Path(base),
        parse_fallback_paths(fallback),
        Path(output),
        parse_override_ranges(args.override_ranges),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
