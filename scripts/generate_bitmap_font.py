# -*- coding: utf-8 -*-
"""将 TTF 矢量字库转换为点阵字库二进制文件。

用法:
    python generate_bitmap_font.py --font font.ttf --output font.bin \
        --size 32 --bpp 8

输出二进制格式 (小端):
    Header (32 字节):
        magic[4]      = "JYBF"
        version       uint8   = 1
        bpp           uint8   (1/2/4/8)
        base_size     uint8   (基准字号, 像素)
        line_height   uint8   (基准字号下的行高, 像素)
        base_line     uint8   (基准字号下的基线距底部, 像素)
        glyph_count   uint32
        reserved[18]

    Glyph table (按 unicode 升序, 每项 24 字节):
        unicode       uint32
        adv_w         uint32  (基准字号下的 advance width, 像素)
        box_w         uint16  (字形包围盒宽度)
        box_h         uint16  (字形包围盒高度)
        ofs_x         int16   (左侧 bearing, 原点向右为正)
        ofs_y         int16   (LVGL 约定: bitmap_top - box_h,
                               bitmap_top 为字形顶部到基线的距离, 向上为正)
        bitmap_offset uint32  (位图数据区偏移)
        bitmap_size   uint32  (位图字节数)

    Bitmap data:
        按 bpp 打包的灰度位图, 逐行存储, MSB 优先。
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageFont

HEADER_MAGIC = b"JYBF"
HEADER_SIZE = 32
GLYPH_ENTRY_SIZE = 24


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, size: int):
    """渲染单个字符，返回 (adv_w, box_w, box_h, ofs_x, ofs_y, pixels_bytes)。

    使用 L 模式 (8 位灰度) 渲染后按 bpp 打包。

    LVGL ofs_y 约定 (来自 lv_freetype_glyph.c):
        ofs_y = bitmap_top - box_h
    其中 bitmap_top 是字形顶部到基线的距离 (向上为正)。
    LVGL 定位: y1_top = baseline_y - box_h - ofs_y = baseline_y - bitmap_top。

    使用 anchor="ls" (left-baseline) 获取相对基线的 bbox:
        y0 = 字形顶部相对基线 (向上为负)
        y1 = 字形底部相对基线 (向下为正)
        bitmap_top = -y0
        ofs_y = bitmap_top - box_h = -y0 - (y1 - y0) = -y1
    """
    try:
        bbox = font.getbbox(ch, anchor="ls")
    except Exception:
        bbox = None
    if bbox is None:
        return None

    x0, y0, x1, y1 = bbox
    box_w = x1 - x0
    box_h = y1 - y0
    if box_w <= 0 or box_h <= 0:
        # 空格等不可见字符：仍记录 advance，无位图
        adv = font.getlength(ch)
        return (int(round(adv)), 0, 0, 0, 0, b"")

    # 渲染到与字形包围盒等大的画布
    img = Image.new("L", (box_w, box_h), 0)
    from PIL import ImageDraw

    draw = ImageDraw.Draw(img)
    draw.text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    pixels = img.tobytes()

    adv = font.getlength(ch)
    # ofs_x = x0 (左侧 bearing), ofs_y = -y1 (LVGL 约定: bitmap_top - box_h)
    return (int(round(adv)), box_w, box_h, x0, -y1, pixels)


def pack_pixels(raw: bytes, bpp: int) -> bytes:
    """将 8bpp 灰度按目标 bpp 打包。"""
    if bpp == 8:
        return raw
    if bpp == 1:
        out = bytearray()
        for i in range(0, len(raw), 8):
            byte = 0
            for j in range(8):
                if i + j < len(raw) and raw[i + j] >= 128:
                    byte |= 1 << (7 - j)
            out.append(byte)
        return bytes(out)
    if bpp == 2:
        out = bytearray()
        for i in range(0, len(raw), 4):
            byte = 0
            for j in range(4):
                v = raw[i + j] if i + j < len(raw) else 0
                v = min(3, v >> 6)
                byte |= v << (6 - j * 2)
            out.append(byte)
        return bytes(out)
    if bpp == 4:
        out = bytearray()
        for i in range(0, len(raw), 2):
            hi = min(15, raw[i] >> 4)
            lo = min(15, raw[i + 1] >> 4) if i + 1 < len(raw) else 0
            out.append((hi << 4) | lo)
        return bytes(out)
    raise ValueError(f"unsupported bpp: {bpp}")


def compute_line_metrics(ttf: TTFont, size: int):
    """根据 TTF 的 hhea/ascent/descent 计算行高和基线。"""
    hhea = ttf["hhea"]
    upem = ttf["head"].unitsPerEm
    scale = size / upem
    ascent = hhea.ascent
    descent = hhea.descent
    line_gap = getattr(hhea, "lineGap", 0)
    line_height = int(round((ascent - descent + line_gap) * scale))
    # base_line: 基线距行底部的距离 = descent 的绝对值 (向下为正)
    base_line = int(round(-descent * scale))
    return line_height, base_line


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert TTF to bitmap font binary")
    parser.add_argument("--font", type=Path, required=True, help="输入 TTF 文件")
    parser.add_argument("--output", type=Path, required=True, help="输出点阵字库 bin 文件")
    parser.add_argument("--size", type=int, default=32, help="基准字号 (像素)")
    parser.add_argument("--bpp", type=int, default=8, choices=[1, 2, 4, 8], help="位深")
    args = parser.parse_args()

    if not args.font.exists():
        print(f"[ERROR] font not found: {args.font}", file=sys.stderr)
        return 1

    ttf = TTFont(args.font)
    cmap = ttf.getBestCmap()
    font = ImageFont.truetype(str(args.font), args.size)

    line_height, base_line = compute_line_metrics(ttf, args.size)

    # 收集所有字形 (按 unicode 升序)
    glyph_entries = []
    bitmap_data = bytearray()
    bitmap_offset = 0

    codepoints = sorted(cmap.keys())
    total = len(codepoints)
    for idx, uni in enumerate(codepoints):
        if uni >= 0x110000:
            continue
        ch = chr(uni)
        result = render_glyph(font, ch, args.size)
        if result is None:
            continue
        adv_w, box_w, box_h, ofs_x, ofs_y, raw = result
        packed = pack_pixels(raw, args.bpp) if raw else b""
        entry = struct.pack(
            "<IIHHhhII",
            uni & 0xFFFFFFFF,
            adv_w & 0xFFFFFFFF,
            box_w & 0xFFFF,
            box_h & 0xFFFF,
            ofs_x,
            ofs_y,
            0,  # placeholder for bitmap_offset
            len(packed),
        )
        # store for later fix-up
        glyph_entries.append((uni, adv_w, box_w, box_h, ofs_x, ofs_y, len(packed), packed))
        if (idx + 1) % 5000 == 0:
            print(f"  rendered {idx + 1}/{total} glyphs...", file=sys.stderr)

    glyph_count = len(glyph_entries)
    table_offset = HEADER_SIZE
    data_offset = HEADER_SIZE + glyph_count * GLYPH_ENTRY_SIZE

    # 写 header
    out = bytearray()
    out += HEADER_MAGIC
    out += struct.pack("<BBB", 1, args.bpp, args.size)
    out += struct.pack("<BB", line_height & 0xFF, base_line & 0xFF)
    out += struct.pack("<I", glyph_count)
    out += b"\x00" * (HEADER_SIZE - len(out))  # pad reserved

    # 写字形表 (修正 bitmap_offset = data_offset + cumulative)
    cur_data_offset = 0
    for (uni, adv_w, box_w, box_h, ofs_x, ofs_y, bmp_size, packed) in glyph_entries:
        out += struct.pack(
            "<IIHHhhII",
            uni,
            adv_w,
            box_w,
            box_h,
            ofs_x,
            ofs_y,
            data_offset + cur_data_offset,
            bmp_size,
        )
        cur_data_offset += bmp_size

    # 写位图数据
    for (_, _, _, _, _, _, _, packed) in glyph_entries:
        out += packed

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(out)

    total_size = len(out)
    print(
        f"[OK] bitmap font generated: {args.output}  "
        f"glyphs={glyph_count}  size={args.size}px  bpp={args.bpp}  "
        f"line_height={line_height}  base_line={base_line}  "
        f"total={total_size} bytes ({total_size / 1024 / 1024:.2f} MB)",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
