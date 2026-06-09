# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import csv
import json
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class GenerateResult:
    """记录国际化 JSON 生成结果。"""

    locales: list[str]
    string_count: int


def clean_json_files(output_dir: Path) -> None:
    """清理输出目录里已有的语言 JSON 文件。"""

    output_dir.mkdir(parents=True, exist_ok=True)
    for entry in output_dir.iterdir():
        if entry.is_file() and entry.suffix == ".json":
            entry.unlink()


def clean_csv_value(value: str | None) -> str:
    """清理 CSV 对齐空格，并保留多行文案的换行语义。"""

    text = (value or "").strip()
    if "\n" not in text and "\r" not in text:
        return text
    return "\n".join(
        line.strip()
        for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    )


def generate_i18n_json(csv_path: Path | str, output_dir: Path | str) -> GenerateResult:
    """从 StringPool.csv 生成 system/i18n 使用的多语言 JSON。"""

    csv_path = Path(csv_path)
    output_dir = Path(output_dir)
    if not csv_path.is_file():
        raise FileNotFoundError(f"StringPool.csv not found: {csv_path}")

    with csv_path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f, skipinitialspace=True)
        fieldnames = [(name or "").strip() for name in (reader.fieldnames or [])]
        reader.fieldnames = fieldnames
        if not fieldnames or fieldnames[0] != "StringID":
            raise ValueError("StringPool.csv header must start with StringID")

        locales = [name.strip() for name in fieldnames[1:] if name and name.strip()]
        if not locales:
            raise ValueError("StringPool.csv has no locale columns")

        data_by_locale = {locale: {} for locale in locales}
        for row in reader:
            string_id = (row.get("StringID") or "").strip()
            if not string_id:
                continue
            for locale in locales:
                data_by_locale[locale][string_id] = clean_csv_value(row.get(locale))

    clean_json_files(output_dir)
    for locale, data in data_by_locale.items():
        out_path = output_dir / f"{locale}.json"
        with out_path.open("w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
            f.write("\n")

    return GenerateResult(locales=locales, string_count=len(data_by_locale[locales[0]]))


def main() -> int:
    parser = argparse.ArgumentParser(description="StringPool CSV to i18n JSON generator")
    parser.add_argument("--csv", required=True, help="StringPool.csv 路径")
    parser.add_argument("--json-out", required=True, help="i18n JSON 输出目录")
    args = parser.parse_args()

    start = time.time()
    result = generate_i18n_json(args.csv, args.json_out)
    cost_s = round(time.time() - start)
    print(
        "已生成 {} 种语言，每种 {} 条字符串: {}".format(
            len(result.locales), result.string_count, args.json_out
        )
    )
    print("生成完成，运行时长: {} min {} s".format(cost_s // 60, cost_s % 60))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
