#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <input_elf> <output_elf>"
    exit 1
fi

INPUT_ELF=$1
OUTPUT_ELF=$2
KEEP_SYMS_FILE="$(dirname "$INPUT_ELF")/keep_syms.txt"

if [ ! -f "$KEEP_SYMS_FILE" ]; then
    echo "Error: keep_syms.txt not found at $KEEP_SYMS_FILE"
    exit 1
fi

KEEP_SYMBOLS=$(cat "$KEEP_SYMS_FILE" | grep -v '^#' | grep -v '^$' | awk '{print "-K "$0}' | tr '\n' ' ')
arm-none-eabi-objcopy --strip-all $KEEP_SYMBOLS "$INPUT_ELF" "$OUTPUT_ELF"