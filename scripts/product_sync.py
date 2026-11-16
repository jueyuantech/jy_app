from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path


def print_info(message: str) -> None:
    print(f"[INFO] {message}")


def print_success(message: str) -> None:
    print(f"[SUCCESS] {message}")


def print_warning(message: str) -> None:
    print(f"[WARNING] {message}")


def print_error(message: str) -> None:
    print(f"[ERROR] {message}", file=sys.stderr)


def is_valid_product_name(name: str) -> bool:
    return bool(re.fullmatch(r"[A-Za-z0-9_-]+", name))


def list_products(products_dir: Path) -> list[str]:
    if not products_dir.is_dir():
        return []
    names: list[str] = []
    for child in products_dir.iterdir():
        if child.is_dir() and not child.name.startswith("."):
            names.append(child.name)
    return sorted(names)


PRODUCT_FILE_OVERLAYS = (
    (Path("apps") / "home" / "home_cfg.c", Path("apps") / "home" / "home_cfg.c"),
    (Path("StringPool.csv"), Path("StringPool.csv")),
)

PRODUCT_TREE_SYNCS = (
    Path("lfsd"),
)

UI_RES_JSON = Path("ui.res.json")
JYTC_SYSTEM_IMAGES_DIR = "/jyt_c/system/images"
JYTC_SYSTEM_AUDIO_DIR = "/jyt_c/system/audio"


def clear_directory_contents(directory: Path, preserve_names: set[str] | None = None) -> None:
    if not directory.exists():
        directory.mkdir(parents=True, exist_ok=True)
        return
    if not directory.is_dir():
        raise RuntimeError(f"not a directory: {directory}")
    preserve_names = preserve_names or set()
    for entry in directory.iterdir():
        if entry.name in preserve_names:
            continue
        if entry.is_dir():
            shutil.rmtree(entry)
        else:
            entry.unlink()


def copy_tree(src_dir: Path, dst_dir: Path) -> tuple[int, int]:
    if not src_dir.is_dir():
        return 0, 0

    total = 0
    copied = 0
    for root, dirs, files in os.walk(src_dir):
        dirs[:] = [d for d in dirs if d not in ("__pycache__", ".git", ".svn")]
        root_path = Path(root)
        rel_root = root_path.relative_to(src_dir)
        for filename in files:
            total += 1
            src_file = root_path / filename
            rel_file = rel_root / filename
            dst_file = dst_dir / rel_file
            dst_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_file, dst_file)
            copied += 1
    return total, copied


def copy_file(src_file: Path, dst_file: Path) -> None:
    if not src_file.is_file():
        raise RuntimeError(f"product file not found: {src_file}")
    dst_file.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src_file, dst_file)


def remove_file_if_exists(path: Path) -> bool:
    if not path.exists():
        return False
    if path.is_dir():
        raise RuntimeError(f"expected file, got directory: {path}")
    path.unlink()
    return True


def file_resource_id(file_path: Path, fallback_prefix: str) -> str:
    resource_id = re.sub(r"[^0-9A-Za-z_]", "_", file_path.stem)
    resource_id = resource_id.strip("_")
    if not resource_id or resource_id[0].isdigit():
        resource_id = f"{fallback_prefix}_{resource_id}"
    return resource_id


def generate_ui_res_json(images_dir: Path, audio_dir: Path, output_file: Path) -> None:
    images: dict[str, dict[str, str]] = {}
    for image_file in sorted((item for item in images_dir.iterdir() if item.is_file()), key=lambda p: p.name):
        resource_id = file_resource_id(image_file, "img")
        if resource_id in images:
            raise RuntimeError(f"duplicate image resource id: {resource_id}")
        images[resource_id] = {
            "path": f"{JYTC_SYSTEM_IMAGES_DIR}/{image_file.name}",
        }

    audio: dict[str, dict[str, str]] = {}
    for audio_file in sorted((item for item in audio_dir.iterdir() if item.is_file()), key=lambda p: p.name):
        resource_id = file_resource_id(audio_file, "audio")
        if resource_id in audio:
            raise RuntimeError(f"duplicate audio resource id: {resource_id}")
        audio[resource_id] = {
            "path": f"{JYTC_SYSTEM_AUDIO_DIR}/{audio_file.name}",
        }

    output_file.write_text(
        json.dumps({"name": "ui", "images": images, "audio": audio}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def clean_product_overlay(repo_root: Path) -> None:
    overlay_files = {dst for _, dst in PRODUCT_FILE_OVERLAYS}
    overlay_files.add(UI_RES_JSON)

    images_dst = repo_root / "lfsc" / "system" / "images"
    print_info(f"clearing jyt_c images: {images_dst}")
    clear_directory_contents(images_dst, preserve_names={".gitkeep"})

    audio_dst = repo_root / "lfsc" / "system" / "audio"
    print_info(f"clearing jyt_c audio: {audio_dst}")
    clear_directory_contents(audio_dst, preserve_names={".gitkeep"})

    for tree_rel in PRODUCT_TREE_SYNCS:
        dst_tree = repo_root / tree_rel
        print_info(f"clearing product tree: {dst_tree}")
        clear_directory_contents(dst_tree)

    removed = 0
    for rel_path in overlay_files:
        if remove_file_if_exists(repo_root / rel_path):
            removed += 1
    print_success(
        f"product overlay cleaned: removed_files={removed}, "
        f"trees_cleared={len(PRODUCT_TREE_SYNCS)}"
    )


def sync_product_trees(repo_root: Path, product_dir: Path) -> None:
    for tree_rel in PRODUCT_TREE_SYNCS:
        src_tree = product_dir / tree_rel
        dst_tree = repo_root / tree_rel
        if not src_tree.is_dir():
            raise RuntimeError(f"product tree not found: {src_tree}")
        print_info(f"clearing product tree: {dst_tree}")
        clear_directory_contents(dst_tree)
        print_info(f"copy tree: {src_tree} -> {dst_tree}")
        tree_total, tree_copied = copy_tree(src_tree, dst_tree)
        print_success(f"tree synced: {tree_rel.as_posix()} {tree_copied}/{tree_total}")


def apply_product_overlay(repo_root: Path, product_name: str) -> None:
    products_dir = repo_root / "products"
    available = list_products(products_dir)

    if not is_valid_product_name(product_name):
        raise RuntimeError(
            f"invalid product name: {product_name}\n"
            f"allowed pattern: [A-Za-z0-9_-]+\n"
            f"available: {', '.join(available) if available else '(none)'}"
        )

    product_dir = products_dir / product_name
    if not product_dir.is_dir():
        raise RuntimeError(
            f"product not found: {product_dir}\n"
            f"available: {', '.join(available) if available else '(none)'}"
        )

    print_info(f"repo_root: {repo_root}")
    print_info(f"product:   {product_name}")
    print_info(f"source:    {product_dir}")

    images_src = product_dir / "images"
    images_dst = repo_root / "lfsc" / "system" / "images"
    if not images_src.is_dir():
        raise RuntimeError(f"product images not found: {images_src}")
    print_info(f"clearing jyt_c images: {images_dst}")
    clear_directory_contents(images_dst, preserve_names={".gitkeep"})
    print_info(f"copy images from: {images_src} -> {images_dst}")
    img_total, img_copied = copy_tree(images_src, images_dst)
    print_success(f"jyt_c images copied: {img_copied}/{img_total}")

    audio_src = product_dir / "audio"
    audio_dst = repo_root / "lfsc" / "system" / "audio"
    if not audio_src.is_dir():
        raise RuntimeError(f"product audio resources not found: {audio_src}")
    print_info(f"clearing jyt_c audio: {audio_dst}")
    clear_directory_contents(audio_dst, preserve_names={".gitkeep"})
    print_info(f"copy audio from: {audio_src} -> {audio_dst}")
    audio_total, audio_copied = copy_tree(audio_src, audio_dst)
    print_success(f"jyt_c audio copied: {audio_copied}/{audio_total}")

    ui_res_json = repo_root / UI_RES_JSON
    print_info(f"generate resource json: {ui_res_json}")
    generate_ui_res_json(images_src, audio_src, ui_res_json)
    print_success(f"generated: {UI_RES_JSON.as_posix()}")

    for src_rel, dst_rel in PRODUCT_FILE_OVERLAYS:
        src = product_dir / src_rel
        dst = repo_root / dst_rel
        print_info(f"copy file: {src} -> {dst}")
        copy_file(src, dst)
        print_success(f"overlaid: {dst_rel.as_posix()}")

    sync_product_trees(repo_root, product_dir)

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", help="product name to sync, or 'clean' to remove product overlay")
    args = parser.parse_args()

    script_path = Path(__file__).resolve()
    repo_root = script_path.parent.parent

    try:
        if args.command == "clean":
            clean_product_overlay(repo_root)
        else:
            apply_product_overlay(repo_root, args.command)
    except Exception as exc:  # noqa: BLE001
        print_error(str(exc))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
