#!/usr/bin/env python3
"""Extract a jy_os_sdk archive into a content-addressed cache directory."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True, help="Path to jy_os_sdk 7z archive.")
    parser.add_argument("--dest", type=Path, required=True, help="Cache directory for the extracted archive.")
    parser.add_argument("--sha256", help="Expected full archive SHA256 stored beside the extracted SDK.")
    parser.add_argument("--seven-zip", default="7z", help="7-Zip executable path or command name.")
    return parser.parse_args()


def validate_sdk_root(dest: Path, expected_sha256: str | None = None) -> None:
    manifest = dest / "os_sdk" / "manifest.json"
    if not manifest.is_file():
        raise RuntimeError(f"OS SDK manifest missing: {manifest}")
    if expected_sha256 is not None:
        sha_file = dest / ".archive.sha256"
        if not sha_file.is_file():
            raise RuntimeError(f"OS SDK archive hash marker missing: {sha_file}")
        actual_sha256 = sha_file.read_text(encoding="utf-8").strip().lower()
        if actual_sha256 != expected_sha256.lower():
            raise RuntimeError(
                f"OS SDK cache hash mismatch: expected {expected_sha256}, got {actual_sha256}")


def extract_archive(archive: Path, dest: Path, seven_zip: str, expected_sha256: str | None) -> None:
    if not archive.is_file():
        raise RuntimeError(f"OS SDK archive does not exist: {archive}")

    if dest.is_dir():
        validate_sdk_root(dest, expected_sha256)
        return

    dest.parent.mkdir(parents=True, exist_ok=True)
    temp_root = Path(tempfile.gettempdir()) / f"jy_os_sdk_{dest.name[:8]}_{uuid.uuid4().hex[:8]}"
    temp_root.mkdir(parents=True)
    try:
        subprocess.run(
            [seven_zip, "x", "-y", f"-o{temp_root}", str(archive)],
            check=True,
        )
        validate_sdk_root(temp_root)
        if expected_sha256 is not None:
            (temp_root / ".archive.sha256").write_text(
                expected_sha256.lower() + "\n",
                encoding="utf-8")

        if dest.is_dir():
            validate_sdk_root(dest, expected_sha256)
            return
        shutil.move(str(temp_root), str(dest))
        temp_root = None
    finally:
        if temp_root is not None:
            shutil.rmtree(temp_root, ignore_errors=True)


def main() -> int:
    args = parse_args()
    extract_archive(args.archive.resolve(), args.dest.resolve(), args.seven_zip, args.sha256)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
