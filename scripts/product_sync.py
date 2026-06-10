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

PRODUCT_OPTIONAL_FILE_OVERLAYS = (
    (Path("build_apps_denylist.txt"), Path("build_apps_denylist.txt")),
)

PRODUCT_TREE_OVERLAYS = (
    Path("lfsd"),
)

UI_RES_JSON = Path("ui.res.json")
ROMFS_SYSTEM_IMAGES_DIR = "/romfs/system/images"


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


def prune_empty_dirs(directory: Path, stop_dir: Path) -> None:
    current = directory
    stop_dir = stop_dir.resolve()
    while current.exists():
        current_resolved = current.resolve()
        if current_resolved == stop_dir:
            break
        try:
            current_resolved.relative_to(stop_dir)
        except ValueError:
            break
        try:
            current.rmdir()
        except OSError:
            break
        current = current.parent


def remove_product_tree_overlay_files(repo_root: Path, products_dir: Path) -> int:
    removed = 0
    for product_name in list_products(products_dir):
        product_dir = products_dir / product_name
        for tree_rel in PRODUCT_TREE_OVERLAYS:
            src_tree = product_dir / tree_rel
            dst_tree = repo_root / tree_rel
            if not src_tree.is_dir():
                continue
            for root, dirs, files in os.walk(src_tree):
                dirs[:] = [d for d in dirs if d not in ("__pycache__", ".git", ".svn")]
                rel_root = Path(root).relative_to(src_tree)
                for filename in files:
                    dst_file = dst_tree / rel_root / filename
                    if remove_file_if_exists(dst_file):
                        removed += 1
                        prune_empty_dirs(dst_file.parent, dst_tree)
    return removed


def image_resource_id(image_file: Path) -> str:
    resource_id = re.sub(r"[^0-9A-Za-z_]", "_", image_file.stem)
    resource_id = resource_id.strip("_")
    if not resource_id or resource_id[0].isdigit():
        resource_id = f"img_{resource_id}"
    return resource_id


def generate_ui_res_json(images_dir: Path, output_file: Path) -> None:
    images: dict[str, dict[str, str]] = {}
    for image_file in sorted((item for item in images_dir.iterdir() if item.is_file()), key=lambda p: p.name):
        resource_id = image_resource_id(image_file)
        if resource_id in images:
            raise RuntimeError(f"duplicate image resource id: {resource_id}")
        images[resource_id] = {
            "path": f"{ROMFS_SYSTEM_IMAGES_DIR}/{image_file.name}",
        }

    output_file.write_text(
        json.dumps({"name": "ui", "images": images}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def clean_product_overlay(repo_root: Path) -> None:
    overlay_files = {dst for _, dst in PRODUCT_FILE_OVERLAYS}
    overlay_files.update(dst for _, dst in PRODUCT_OPTIONAL_FILE_OVERLAYS)
    overlay_files.add(UI_RES_JSON)
    products_dir = repo_root / "products"

    images_dst = repo_root / "romfs" / "system" / "images"
    print_info(f"clearing romfs images: {images_dst}")
    clear_directory_contents(images_dst, preserve_names={".gitkeep"})

    removed = 0
    removed += remove_product_tree_overlay_files(repo_root, products_dir)
    for rel_path in overlay_files:
        if remove_file_if_exists(repo_root / rel_path):
            removed += 1
    print_success(f"product overlay cleaned: removed={removed}")


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
    images_dst = repo_root / "romfs" / "system" / "images"
    if not images_src.is_dir():
        raise RuntimeError(f"product images not found: {images_src}")
    print_info(f"clearing romfs images: {images_dst}")
    clear_directory_contents(images_dst, preserve_names={".gitkeep"})
    print_info(f"copy images from: {images_src} -> {images_dst}")
    img_total, img_copied = copy_tree(images_src, images_dst)
    print_success(f"romfs images copied: {img_copied}/{img_total}")

    ui_res_json = repo_root / UI_RES_JSON
    print_info(f"generate resource json: {ui_res_json}")
    generate_ui_res_json(images_src, ui_res_json)
    print_success(f"generated: {UI_RES_JSON.as_posix()}")

    for src_rel, dst_rel in PRODUCT_FILE_OVERLAYS:
        src = product_dir / src_rel
        dst = repo_root / dst_rel
        print_info(f"copy file: {src} -> {dst}")
        copy_file(src, dst)
        print_success(f"overlaid: {dst_rel.as_posix()}")

    for tree_rel in PRODUCT_TREE_OVERLAYS:
        src_tree = product_dir / tree_rel
        dst_tree = repo_root / tree_rel
        if src_tree.is_dir():
            print_info(f"copy tree: {src_tree} -> {dst_tree}")
            tree_total, tree_copied = copy_tree(src_tree, dst_tree)
            print_success(f"tree overlaid: {tree_rel.as_posix()} {tree_copied}/{tree_total}")

    for src_rel, dst_rel in PRODUCT_OPTIONAL_FILE_OVERLAYS:
        src = product_dir / src_rel
        dst = repo_root / dst_rel
        if src.is_file():
            print_info(f"copy file: {src} -> {dst}")
            copy_file(src, dst)
            print_success(f"overlaid: {dst_rel.as_posix()}")
        elif remove_file_if_exists(dst):
            print_info(f"removed optional overlay: {dst_rel.as_posix()}")


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
