#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from littlefs import LittleFS


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

DEFAULT_SOURCE = Path("./uimain")
SYMBOL_FILE = REPO_ROOT / "bes28" / "SymbolTable.def"
LFSC_DIR = REPO_ROOT / "lfsc"
LFSD_DIR = REPO_ROOT / "lfsd"
STRING_POOL_CSV = REPO_ROOT / "StringPool.csv"
STRING_POOL_SCRIPT = REPO_ROOT / "scripts" / "StringPool.py"
LFSC_BIN = Path("./nuttx_lfsc.bin")
LFSD_BIN = Path("./nuttx_lfsd.bin")
ROMFS_DIR = REPO_ROOT / "romfs"
ROMFS_BIN = Path("./nuttx_romfs.bin")
ROMFS_SIZE = 16 * 1024 * 1024  # 0x1000000
LFSC_SIZE = 2 * 1024 * 1024
LFSD_SIZE = 6 * 1024 * 1024    # 0x600000
MAX_FILE_SIZE = 2 * 1024 * 1024
ROMFS_ALIGNMENT = 16
ROMFS_NAME_MAX = 255
ROMFS_MAGIC = b"-rom1fs-"
ROMFS_FIRST_CHECKSUM_SIZE = 512
RFNEXT_DIRECTORY = 1
RFNEXT_FILE = 2
RFNEXT_HARDLINK = 0
RFNEXT_EXEC = 8
ROMFS_IMAGE_PADDING = 4096


@dataclass
class RomfsNode:
    name: str
    source_path: Path
    is_dir: bool
    children: list["RomfsNode"] = field(default_factory=list)
    data: bytes = b""
    offset: int = 0
    total_size: int = 0
    parent: "RomfsNode | None" = None
    dot_offset: int = 0
    dotdot_offset: int = 0


@dataclass(frozen=True)
class FilesystemOutputs:
    lfsc: Path
    lfsd: Path
    romfs: Path


def default_filesystem_outputs() -> FilesystemOutputs:
    return FilesystemOutputs(
        lfsc=LFSC_BIN,
        lfsd=LFSD_BIN,
        romfs=ROMFS_BIN,
    )


def print_info(message: str) -> None:
    print(f"[INFO] {message}")


def print_success(message: str) -> None:
    print(f"[SUCCESS] {message}")


def print_warning(message: str) -> None:
    print(f"[WARNING] {message}")


def print_error(message: str) -> None:
    print(f"[ERROR] {message}", file=sys.stderr)


def run_command(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=True,
        text=True,
        capture_output=True,
        **kwargs,
    )


def check_file_size(file_path: Path, max_size: int) -> None:
    if not file_path.is_file():
        raise FileNotFoundError(f"file does not exist: {file_path}")

    file_size = file_path.stat().st_size
    print_info(f"文件大小: {file_size} 字节, 限制: {max_size} 字节")

    if file_size > max_size:
        raise RuntimeError(f"文件 {file_path} 超过大小限制 ({file_size} > {max_size})")

    print_success("文件大小检查通过")


def read_symbol_table(symbol_file: Path) -> set[str]:
    text = symbol_file.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    cleaned_lines: list[str] = []

    for raw_line in text.splitlines():
        line = raw_line.split("//", 1)[0].strip()
        if not line:
            continue
        cleaned_lines.extend(part.strip() for part in line.split(",") if part.strip())

    return set(cleaned_lines)


def get_undefined_symbols(program_file: Path) -> list[str]:
    result = run_command(["arm-none-eabi-nm", str(program_file)])
    symbols: list[str] = []

    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] == "U":
            symbols.append(parts[-1])

    return sorted(set(symbols))


def check_symbols(program_file: Path, symbol_file: Path) -> None:
    print_info("开始符号表检查...")
    print_info(f"程序文件: {program_file}")
    print_info(f"符号表文件: {symbol_file}")

    if not program_file.is_file():
        raise FileNotFoundError(f"程序文件不存在: {program_file}")

    if not symbol_file.is_file():
        raise FileNotFoundError(f"符号表文件不存在: {symbol_file}")

    undefined_symbols = get_undefined_symbols(program_file)
    if not undefined_symbols:
        print_success("程序中没有未定义符号")
        return

    symbol_table = read_symbol_table(symbol_file)
    missing = [symbol for symbol in undefined_symbols if symbol not in symbol_table]

    for symbol in undefined_symbols:
        if symbol in symbol_table:
            print(f"  OK {symbol}")
        else:
            print(f"  MISSING {symbol}")

    print_info(f"总符号数: {len(undefined_symbols)}")
    print_info(f"找到的符号: {len(undefined_symbols) - len(missing)}")
    print_info(f"缺失的符号: {len(missing)}")

    if missing:
        raise RuntimeError(f"有 {len(missing)} 个符号在符号表中未找到")

    print_success("所有符号都在符号表中找到，符号表完全符合！")


def calc_dir_size(path: Path) -> int:
    total = 0
    for root, _, files in os.walk(path):
        for name in files:
            try:
                total += (Path(root) / name).stat().st_size
            except OSError:
                pass
    return total


def align_romfs(value: int) -> int:
    return (value + ROMFS_ALIGNMENT - 1) & ~(ROMFS_ALIGNMENT - 1)


def romfs_name_bytes(name: str) -> bytes:
    encoded = name.encode("utf-8")
    if not encoded:
        raise RuntimeError("ROMFS entry name cannot be empty")
    if b"\0" in encoded:
        raise RuntimeError(f"ROMFS entry name contains NUL: {name}")
    if len(encoded) > ROMFS_NAME_MAX:
        raise RuntimeError(f"ROMFS entry name is too long: {name}")
    return encoded


def romfs_header_size(name: str) -> int:
    return align_romfs(16 + len(romfs_name_bytes(name)) + 1)


def collect_romfs_node(path: Path) -> RomfsNode:
    if path.is_symlink():
        raise RuntimeError(f"ROMFS does not support symlink entries: {path}")

    if path.is_dir():
        children = [collect_romfs_node(item) for item in sorted(path.iterdir(), key=lambda p: p.name)]
        if not children:
            raise RuntimeError(f"ROMFS empty directory is not supported: {path}")
        node = RomfsNode(name=path.name, source_path=path, is_dir=True, children=children)
        for child in children:
            child.parent = node
        return node

    if path.is_file():
        return RomfsNode(name=path.name, source_path=path, is_dir=False, data=path.read_bytes())

    raise RuntimeError(f"ROMFS only supports regular files and directories: {path}")


def calc_romfs_node_size(node: RomfsNode) -> int:
    node.total_size = romfs_header_size(node.name)
    if node.is_dir:
        node.total_size += romfs_header_size(".") + romfs_header_size("..")
        node.total_size += sum(calc_romfs_node_size(child) for child in sorted_romfs_children(node.children))
    else:
        node.total_size += align_romfs(len(node.data))
    return node.total_size


def split_romfs_children(children: list[RomfsNode]) -> tuple[list[RomfsNode], list[RomfsNode]]:
    directories = [child for child in children if child.is_dir]
    files = [child for child in children if not child.is_dir]
    return directories, files


def sorted_romfs_children(children: list[RomfsNode]) -> list[RomfsNode]:
    directories, files = split_romfs_children(children)
    return directories + files


def assign_romfs_offsets(nodes: list[RomfsNode], start_offset: int) -> int:
    offset = start_offset
    for node in nodes:
        node.offset = offset
        if node.is_dir:
            child_offset = offset + romfs_header_size(node.name)
            node.dotdot_offset = child_offset
            child_offset += romfs_header_size("..")
            directories, files = split_romfs_children(node.children)
            child_offset = assign_romfs_offsets(directories, child_offset)
            node.dot_offset = child_offset
            child_offset += romfs_header_size(".")
            assign_romfs_offsets(files, child_offset)
        offset += node.total_size
    return offset


def write_u32_be(buffer: bytearray, offset: int, value: int) -> None:
    buffer[offset:offset + 4] = value.to_bytes(4, byteorder="big")


def sum_u32_be(buffer: bytearray, offset: int, size: int) -> int:
    total = 0
    for pos in range(offset, offset + size, 4):
        total = (total + int.from_bytes(buffer[pos:pos + 4], byteorder="big")) & 0xFFFFFFFF
    return total


def write_romfs_checksum(buffer: bytearray, checksum_offset: int, start_offset: int, size: int) -> None:
    write_u32_be(buffer, checksum_offset, 0)
    write_u32_be(buffer, checksum_offset, (-sum_u32_be(buffer, start_offset, size)) & 0xFFFFFFFF)


def write_romfs_entry(
    buffer: bytearray,
    offset: int,
    name: str,
    mode: int,
    info: int,
    size: int,
    next_offset: int,
) -> None:
    name_data = romfs_name_bytes(name)
    header_size = romfs_header_size(name)

    write_u32_be(buffer, offset, (next_offset & ~0xF) | mode)
    write_u32_be(buffer, offset + 4, info)
    write_u32_be(buffer, offset + 8, size)
    write_u32_be(buffer, offset + 12, 0)
    buffer[offset + 16:offset + 16 + len(name_data)] = name_data
    write_romfs_checksum(buffer, offset + 12, offset, header_size)


def write_romfs_node(buffer: bytearray, node: RomfsNode, next_offset: int) -> None:
    header_size = romfs_header_size(node.name)
    mode = (RFNEXT_DIRECTORY | RFNEXT_EXEC) if node.is_dir else RFNEXT_FILE
    info = node.dotdot_offset if node.is_dir else 0
    size = 0 if node.is_dir else len(node.data)

    write_romfs_entry(buffer, node.offset, node.name, mode, info, size, next_offset)

    if node.is_dir:
        directories, files = split_romfs_children(node.children)
        dot_next = files[0].offset if files else 0
        dotdot_next = directories[0].offset if directories else node.dot_offset
        dot_target = node.offset
        dotdot_target = node.parent.offset if node.parent else node.offset
        write_romfs_entry(buffer, node.dotdot_offset, "..", RFNEXT_HARDLINK, dotdot_target, 0, dotdot_next)
        for index, child in enumerate(directories):
            child_next = directories[index + 1].offset if index + 1 < len(directories) else node.dot_offset
            write_romfs_node(buffer, child, child_next)
        write_romfs_entry(buffer, node.dot_offset, ".", RFNEXT_HARDLINK, dot_target, 0, dot_next)
        for index, child in enumerate(files):
            child_next = files[index + 1].offset if index + 1 < len(files) else 0
            write_romfs_node(buffer, child, child_next)
        return

    data_offset = node.offset + header_size
    buffer[data_offset:data_offset + len(node.data)] = node.data


def build_romfs_image(source_dir: Path, volume_label: str) -> bytes:
    volume_name = volume_label.encode("utf-8")
    if not volume_name or b"\0" in volume_name:
        raise RuntimeError(f"invalid ROMFS volume label: {volume_label}")

    root_children = [collect_romfs_node(item) for item in sorted(source_dir.iterdir(), key=lambda p: p.name)]
    if not root_children:
        raise RuntimeError(f"ROMFS source directory is empty: {source_dir}")

    root_node = RomfsNode(name=".", source_path=source_dir, is_dir=True, children=root_children)
    for node in root_children:
        node.parent = root_node

    sorted_children = sorted_romfs_children(root_node.children)
    directories, files = split_romfs_children(root_node.children)
    for node in sorted_children:
        calc_romfs_node_size(node)

    root_offset = align_romfs(16 + len(volume_name) + 1)
    root_node.offset = root_offset
    root_node.dot_offset = root_offset
    root_node.dotdot_offset = root_offset + romfs_header_size(".")
    first_child_offset = root_node.dotdot_offset + romfs_header_size("..")
    first_child_offset = assign_romfs_offsets(directories, first_child_offset)
    first_file_offset = first_child_offset
    volume_size = assign_romfs_offsets(files, first_file_offset)
    image_size = (volume_size + ROMFS_IMAGE_PADDING - 1) & ~(ROMFS_IMAGE_PADDING - 1)
    image = bytearray(image_size)

    image[:len(ROMFS_MAGIC)] = ROMFS_MAGIC
    write_u32_be(image, 8, volume_size)
    write_u32_be(image, 12, 0)
    image[16:16 + len(volume_name)] = volume_name

    write_romfs_entry(
        image,
        root_node.dot_offset,
        ".",
        RFNEXT_DIRECTORY | RFNEXT_EXEC,
        root_node.offset,
        0,
        root_node.dotdot_offset,
    )
    root_dotdot_next = directories[0].offset if directories else (files[0].offset if files else 0)
    write_romfs_entry(image, root_node.dotdot_offset, "..", RFNEXT_HARDLINK, root_node.offset, 0, root_dotdot_next)

    for index, node in enumerate(directories):
        next_offset = directories[index + 1].offset if index + 1 < len(directories) else (files[0].offset if files else 0)
        write_romfs_node(image, node, next_offset)
    for index, node in enumerate(files):
        next_offset = files[index + 1].offset if index + 1 < len(files) else 0
        write_romfs_node(image, node, next_offset)

    write_romfs_checksum(image, 12, 0, min(ROMFS_FIRST_CHECKSUM_SIZE, image_size))
    return bytes(image)


def copy_directory_to_lfs(fs: LittleFS, src_dir: Path, dest_dir: str) -> None:
    if dest_dir != "/":
        try:
            fs.mkdir(dest_dir)
        except Exception:
            pass

    for item in src_dir.iterdir():
        dest_path = (f"{dest_dir}/{item.name}" if dest_dir != "/" else f"/{item.name}").replace("\\", "/")
        if item.is_dir():
            try:
                fs.mkdir(dest_path)
            except Exception:
                pass
            copy_directory_to_lfs(fs, item, dest_path)
        else:
            content = item.read_bytes()
            with fs.open(dest_path, "wb") as lfs_file:
                lfs_file.write(content)


def create_lfs_image(source_dir: Path, output_file: Path, fs_size: int) -> None:
    if not source_dir.is_dir():
        raise FileNotFoundError(f"source directory not found: {source_dir}")

    total_size = calc_dir_size(source_dir)
    if total_size > fs_size:
        diff = total_size - fs_size
        raise RuntimeError(
            f"源目录 '{source_dir}' 中文件总大小为 {total_size} 字节，超过文件系统容量 {fs_size} 字节，超出 {diff} 字节。"
        )

    block_size = 4096
    block_count = fs_size // block_size
    fs = LittleFS(block_size=block_size, block_count=block_count, name_max=96)
    copy_directory_to_lfs(fs, source_dir, "/")
    output_file.write_bytes(fs.context.buffer)
    print_success(f"LittleFS image created: {output_file}")


def copy_tree_contents(source_dir: Path, target_dir: Path) -> None:
    for item in source_dir.iterdir():
        destination = target_dir / item.name
        if item.is_dir():
            shutil.copytree(item, destination, dirs_exist_ok=True)
        else:
            shutil.copy2(item, destination)


def generate_i18n_json(lfsd_dir: Path) -> None:
    output_dir = lfsd_dir / "system" / "i18n"
    print_info(f"生成i18n JSON: {STRING_POOL_CSV} -> {output_dir}")
    result = run_command([
        sys.executable,
        str(STRING_POOL_SCRIPT),
        "--csv",
        str(STRING_POOL_CSV),
        "--json-out",
        str(output_dir),
    ])
    if result.stdout:
        print(result.stdout, end="")


def create_romfs_image(source_dir: Path, output_file: Path, volume_label: str = "romfs") -> None:
    if not source_dir.is_dir():
        raise FileNotFoundError(f"ROMFS source directory not found: {source_dir}")

    total_size = calc_dir_size(source_dir)
    print_info(f"ROMFS source size: {total_size} bytes, partition limit: {ROMFS_SIZE} bytes")
    if total_size > ROMFS_SIZE:
        raise RuntimeError(f"ROMFS content ({total_size} bytes) exceeds partition size ({ROMFS_SIZE} bytes)")

    genromfs = shutil.which("genromfs")
    if genromfs:
        run_command([genromfs, "-f", str(output_file), "-d", str(source_dir), "-V", volume_label])
        print_success(f"ROMFS image created by genromfs: {output_file}")
        return

    print_warning("genromfs not found in PATH, fallback to Python ROMFS writer")
    image = build_romfs_image(source_dir, volume_label)
    if len(image) > ROMFS_SIZE:
        raise RuntimeError(f"ROMFS image ({len(image)} bytes) exceeds partition size ({ROMFS_SIZE} bytes)")

    output_file.write_bytes(image)
    print_success(f"ROMFS image created: {output_file}")


def create_filesystem(
    source_file: Path,
    outputs: FilesystemOutputs,
    romfs_dir: Path,
) -> None:
    print_info("开始创建文件系统...")

    if not LFSC_DIR.is_dir():
        raise FileNotFoundError(f"目录不存在: {LFSC_DIR}")

    print_info(f"复制文件: {source_file} -> {LFSC_DIR}")
    shutil.copy2(source_file, LFSC_DIR / source_file.name)

    create_lfs_image(LFSC_DIR, outputs.lfsc, fs_size=LFSC_SIZE)
    with tempfile.TemporaryDirectory(prefix="lfsd_base_") as temp_dir:
        lfsd_source_dir = Path(temp_dir)
        copy_tree_contents(LFSD_DIR, lfsd_source_dir)
        generate_i18n_json(lfsd_source_dir)
        create_lfs_image(lfsd_source_dir, outputs.lfsd, fs_size=LFSD_SIZE)

    create_romfs_image(romfs_dir, outputs.romfs)

    print_success(f"文件系统创建完成: {outputs.lfsc} {outputs.lfsd} {outputs.romfs}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Cross-platform filesystem packaging for ARM builds")
    parser.add_argument("--source", default=str(DEFAULT_SOURCE), help="Path to the built ELF file")
    parser.add_argument("--romfs-dir", default=str(ROMFS_DIR), help="ROMFS source directory")
    parser.add_argument("--max-size", type=int, default=MAX_FILE_SIZE, help="Maximum ELF file size in bytes")
    parser.add_argument("--no-symbol-check", action="store_true", help="Skip undefined symbol verification")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_file = Path(args.source)
    romfs_dir = Path(args.romfs_dir)
    outputs = default_filesystem_outputs()

    print_info("========================================")
    print_info("开始执行自动化构建脚本")
    print_info("========================================")

    if not source_file.is_file():
        print_error(f"源文件不存在: {source_file}")
        return 1
    if not romfs_dir.is_dir():
        print_error(f"ROMFS 源目录不存在: {romfs_dir}")
        return 1

    print_success("源文件检查通过")

    try:
        if not args.no_symbol_check:
            check_symbols(source_file, SYMBOL_FILE)

        check_file_size(source_file, args.max_size)
        create_filesystem(source_file, outputs, romfs_dir)
    except Exception as exc:  # noqa: BLE001
        print_error(str(exc))
        return 1

    print_info("========================================")
    print_success("所有步骤执行完成！")
    print_info("========================================")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
