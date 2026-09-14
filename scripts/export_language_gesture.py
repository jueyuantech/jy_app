"""从 SVG 导出黑底白线语言页资源，保留源图尺寸和透明度。"""

import argparse
import io
from pathlib import Path
import xml.etree.ElementTree as ET

import cairosvg
from PIL import Image


def main():
    """读取指定 SVG，将绿色主题色映射为白色后生成原生尺寸 JPG。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = ET.fromstring(args.source.read_bytes())
    for node in root.iter():
        for attr in ("fill", "stroke"):
            if node.get(attr, "").lower() in ("#00ff00", "#0f0"):
                node.set(attr, "#ffffff")
    png = cairosvg.svg2png(bytestring=ET.tostring(root), background_color="#000000")
    with Image.open(io.BytesIO(png)) as image:
        image.convert("RGB").save(args.output, quality=95, subsampling=0)
        print(f"Exported {image.width}x{image.height}: {args.output}")


if __name__ == "__main__":
    main()
