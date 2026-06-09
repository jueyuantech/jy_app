#!/usr/bin/env python3
"""Generate C resource macros from jy_app resource JSON files."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


def _require_str(data: dict[str, Any], key: str, path: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{path}.{key} must be a non-empty string")
    return value


def _identifier(value: str, path: str) -> str:
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value):
        raise ValueError(f"{path} must be a valid C identifier")
    return value


def _c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def _macro_part(value: str, path: str) -> str:
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value):
        raise ValueError(f"{path} must contain letters, numbers, or underscores")
    return value.upper()


def _load_json(input_path: Path) -> dict[str, Any]:
    with input_path.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict):
        raise ValueError("resource root must be an object")
    return data


def compile_resource_file(input_path: str | Path, output_dir: str | Path) -> Path:
    """Compile one resource JSON file and return the generated header path."""

    input_path = Path(input_path)
    output_dir = Path(output_dir)
    data = _load_json(input_path)

    name = _identifier(_require_str(data, "name", "$"), "$.name")
    images = data.get("images", {})
    if images is None:
        images = {}
    if not isinstance(images, dict):
        raise ValueError("$.images must be an object")

    output_dir.mkdir(parents=True, exist_ok=True)
    guard = f"{_macro_part(name, '$.name')}_RES_H"
    header_path = output_dir / f"{name}_res.h"

    lines: list[str] = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <lvgl/lvgl.h>",
        "",
    ]

    macro_prefix = _macro_part(name, "$.name")
    for image_id in sorted(images):
        image_path = f"$.images.{image_id}"
        image = images[image_id]
        if not isinstance(image, dict):
            raise ValueError(f"{image_path} must be an object")
        macro_name = f"{macro_prefix}_RES_IMAGE_{_macro_part(image_id, image_path)}"
        if "path" in image:
            path = _require_str(image, "path", image_path)
            lines.append(f"#define {macro_name} {_c_string(path)}")
        else:
            c_type = _require_str(image, "type", image_path)
            symbol = _identifier(_require_str(image, "symbol", image_path), f"{image_path}.symbol")
            lines.append(f"extern {c_type} {symbol};")
            lines.append(f"#define {macro_name} (&{symbol})")
        lines.append("")

    lines.append(f"#endif /* {guard} */")
    lines.append("")
    header_path.write_text("\n".join(lines), encoding="utf-8")
    return header_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate jy_app resource headers.")
    parser.add_argument("input", help="Path to .res.json")
    parser.add_argument("--out-dir", required=True, help="Directory for generated files")
    args = parser.parse_args()

    try:
        compile_resource_file(args.input, args.out_dir)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"rcc: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
