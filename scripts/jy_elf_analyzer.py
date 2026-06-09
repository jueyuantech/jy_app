#!/usr/bin/env python3
"""
JY ELF Analyzer - A tool for analyzing ELF files and stack dumps.
"""

import argparse
import os
import sys
import subprocess
import re


def validate_file_exists(file_path, file_type):
    """Validate that the specified file exists."""
    if not file_path:
        print(f"Error: {file_type} file not specified.", file=sys.stderr)
        return False
    if not os.path.exists(file_path):
        print(f"Error: {file_type} file '{file_path}' does not exist.", file=sys.stderr)
        return False
    return True

def extract_text_section_info(elf_file):
    sec_info = dict()
    cmd = ['arm-none-eabi-readelf', '-S','-W', elf_file]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error: Failed to extract section information from ELF file '{elf_file}'.")
        return None
    sections = result.stdout.split("\n")
    for section in sections:
        if 'PROGBITS' not in section:
            continue        
        sec = re.split(r'[\s\[\]]+', section.strip())
        if '.text' not in sec[2]:
            continue
        idx = int(sec[1])
        sec_name = sec[2]
        func_name = sec_name.split('.')[-1]
        size = int(sec[6], 16)
        sec_info[idx] = {'name':func_name, 'size':size, 'sec_name':sec_name}
    return sec_info

def extract_base_address(stack_file):
    print("try to resolv base address from stack dump...")
    try:
        with open(stack_file, 'r') as f:
            stack_lines = f.readlines()
    except Exception as e:
        print(f"Error reading stack file: {e}", file=sys.stderr)
        return False
    
    for line in stack_lines:
        # 检查行是否包含所需的关键字
        if "[m55]" in line and "task:" in line and "process:" in line:
            # 使用正则表达式匹配行末的地址
            match = re.search(r'0x[0-9a-fA-F]+$', line.strip())
            if match:
                addr_str = match.group(0)
                try:
                    # 将地址转换为整数
                    address = int(addr_str, 16)
                    print(f"Found base address: (0x{address:x}) from line:{line.strip()}")
                    return address
                except ValueError:
                    # 如果转换失败，继续处理下一行
                    continue
    
    print("No matching line found in stack dump")
    return None


def relocate_text_section(sec_info: dict, ref_idx, ref_addr):
    if ref_idx not in sec_info:
        print('Error: section not found')
        exit(1)
    
    # 设置参考索引的地址
    sec_info[ref_idx]['addr'] = ref_addr
    
    # 获取所有section的索引并排序
    sorted_indices = sorted(sec_info.keys())
    ref_position = sorted_indices.index(ref_idx)
    
    # 向前遍历，设置参考索引之前的section地址
    for i in range(ref_position - 1, -1, -1):
        idx = sorted_indices[i]
        prev_idx = sorted_indices[i + 1]
        prev_addr = sec_info[prev_idx]['addr']
        if( prev_addr % 4 != 0):
            print(f'Error: section {prev_idx} address {prev_addr} not aligned')
            exit(1)

        current_addr = prev_addr - sec_info[idx]['size']
        if (current_addr % 4 != 0):
            current_addr -= (current_addr % 4)
        sec_info[idx]['addr'] = current_addr 
    
    # 向后遍历，设置参考索引之后的section地址
    for i in range(ref_position + 1, len(sorted_indices)):
        idx = sorted_indices[i]
        prev_idx = sorted_indices[i - 1]
        prev_addr = sec_info[prev_idx]['addr']
        if( prev_addr % 4 != 0):
            print(f'Error: section {prev_idx} address {prev_addr} not aligned')
            exit(1)
        current_addr = sec_info[prev_idx]['addr'] + sec_info[prev_idx]['size']
        if( current_addr % 4 != 0):
            current_addr += (4 - current_addr % 4)
        sec_info[idx]['addr'] = current_addr 

def dump_section_relocate_info(sec_info: dict, output_file=None):
    output_lines = []
    for k, v in sec_info.items():
        line = f'{k:d} {v["addr"]:08x} {v["size"]:08x} {v["name"]}'
        output_lines.append(line)
    
    if output_file:
        with open(output_file, 'w') as f:
            for line in output_lines:
                f.write(line + '\n')
    else:
        for line in output_lines:
            print(line)

def analyze_elf_with_stack(sec_info: dict, stack_file):
    print(f"Using stack dump file: {stack_file}")
    stack_frame = list()

    try:
        with open(stack_file, 'r') as f:
            stack_lines = f.readlines()
    except Exception as e:
        print(f"Error reading stack file: {e}", file=sys.stderr)
        return False
    
    addresses = []
    for line in stack_lines:
        if "[m55] stack_dump:" not in line:
            continue
        addr = re.split(r'[ :]+', line.strip())
        if len(addr) < 9 or re.match(r'[0-9a-f]+', addr[-8]) is None:
            print(f'xxxxxxxxxx WARNING xxxxxxxxxxxxx  abnormal stack line: {line.strip()}')
            continue
        addresses.extend(addr[-8:])
    
    print(f"Found {len(addresses)} addresses in stack dump")

    for addr_str in addresses:
        addr = int(addr_str, 16)

        # 检查addr是否匹配到在sec_info的段
        matched_section = None
        for idx, info in sec_info.items():
            if 'addr' in info:  # 确保地址已计算
                start_addr = info['addr']
                end_addr = info['addr'] + info['size']
                if start_addr <= addr < end_addr:
                    matched_section = (idx, info)
                    break
        
        if matched_section:
            idx, info = matched_section
            in_sec_offset = addr - info['addr']
            stack_frame.append({'sec_name': info['sec_name'], 'offset': in_sec_offset, 'addr': addr})

    return stack_frame


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='Analyze ELF file using stack dump information',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --elf /path/to/firmware.elf --stack /path/to/stack.txt --ref 1,0x400050
  %(prog)s --elf /path/to/firmware.elf --stack /path/to/stack.txt --ref 1,0x400050 --output relocate_info.txt
        """
    )
    
    parser.add_argument(
        '-e', '--elf',
        type=str,
        required=True,
        help='Path to the ELF file to analyze'
    )
    
    parser.add_argument(
        '-s', '--stack',
        type=str,
        required=True,
        help='Path to the stack dump file to use for analysis'
    )
    
    parser.add_argument(
        '-r', '--ref',
        type=str,
        required=False,
        help='Reference information in format "idx,addr" where addr is in hex format (e.g., "1,0x400050")'
    )
    
    parser.add_argument(
        '-o', '--output',
        type=str,
        required=False,
        default="section_relocate_info.txt",
        help='Output file for section relocation info (default: section_relocate_info.txt)'
    )
    
    args = parser.parse_args()
    
    # Validate input files
    elf_valid = validate_file_exists(args.elf, "ELF")
    stack_valid = validate_file_exists(args.stack, "Stack dump")
    
    if not (elf_valid and stack_valid):
        sys.exit(1)
    
    section_info = extract_text_section_info(args.elf)
    
    ref_idx = None
    ref_addr = None
    if args.ref:
        try:
            parts = args.ref.split(',')
            if len(parts) != 2:
                print(f"Error: Invalid reference format. Expected 'idx,addr', got '{args.ref}'", file=sys.stderr)
                sys.exit(1)
            
            ref_idx = int(parts[0])
            ref_addr = int(parts[1], 16)  # Parse as hexadecimal
        except ValueError as e:
            print(f"Error: Invalid reference format. Expected 'idx,addr' where idx is integer and addr is hex, got '{args.ref}'", file=sys.stderr)
            sys.exit(1)
    else:
        ref_idx = 1
        ref_addr = extract_base_address(args.stack)

    if ref_idx is None or ref_addr is None:
        print("invalid ref")
        exit(1)

    ref_addr = ref_addr - (ref_addr % 2) 
    print(f"Reference: idx={ref_idx}, addr=0x{ref_addr:x}")
    relocate_text_section(section_info, ref_idx, ref_addr)


    dump_section_relocate_info(section_info, args.output)
    # Perform the analysis
    stack_frame = analyze_elf_with_stack(section_info, args.stack)
    stack = list()
    if stack_frame:
        for f in stack_frame:
            cmd = ['arm-none-eabi-addr2line', '-e', args.elf, '-j', f['sec_name'], '-f', '-C', f"{f['offset']}"]
            result = subprocess.run(cmd, capture_output=True, text=True)
            r = result.stdout.split('\n')
            func = r[0]
            source_pos = r[1]
            stack.append(f'{f["addr"]:08x} {func:20s} {source_pos}')

    # uni_stack = list(reversed(dict.fromkeys(reversed(stack))))
    print("-------Potential Stack Frame ---------")
    for s in stack: # uni_stack:
        print(s)
    print('Done')        