#!/usr/bin/env python3
"""
patch_irq_vec.py — append IRQ vector chunk ($FFFE/$FFFF) to razemos_kernel.rp6502

Reads irq_handler address from linker map, then rewrites the .rp6502 file
adding a 2-byte memory chunk at $FFFE with the handler address (little-endian).

Usage: patch_irq_vec.py <map_file> <rp6502_file>
"""

import sys, os, re, binascii

map_file  = sys.argv[1]
rom_file  = sys.argv[2]

# --- Find irq_handler address from map (KERNEL segment start + scan) ---
# irq_handler is the first thing in the KERNEL segment after kernel_init.
# It starts with BIT $FFF0 (IRQ ACK) = opcodes 2C F0 FF.
# We find KERNEL segment start from the map, then scan the binary for the pattern.

kernel_start = None
with open(map_file, 'r', errors='replace') as f:
    for line in f:
        m = re.match(r'^KERNEL\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)', line)
        if m:
            kernel_start = int(m.group(1), 16)
            break

if kernel_start is None:
    print(f"[patch_irq_vec] ERROR: KERNEL segment not found in {map_file}", file=sys.stderr)
    sys.exit(1)

# Read kernel ELF binary (flat binary starting at $0200)
elf_file = rom_file.replace('.rp6502', '')
with open(elf_file, 'rb') as f:
    elf_data = f.read()

base_addr = 0x0200
irq_addr = None
# Search for irq_handler entry: STA kzp_tmp0 ($001A) = 85 1A,
# then LDA VIA_T1CL ($FFD4) = AD D4 FF
for i in range(len(elf_data) - 5):
    if (elf_data[i] == 0x85 and elf_data[i+1] == 0x1A and
            elf_data[i+2] == 0xAD and elf_data[i+3] == 0xD4 and elf_data[i+4] == 0xFF):
        candidate = base_addr + i
        if candidate >= kernel_start:
            irq_addr = candidate
            break

if irq_addr is None:
    print(f"[patch_irq_vec] ERROR: irq_handler pattern not found in {elf_file}", file=sys.stderr)
    sys.exit(1)

print(f"[patch_irq_vec] irq_handler = ${irq_addr:04X}")

# --- Read existing .rp6502 ---
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
import rp6502 as r

rom = r.ROM()
rom.add_rom_file(rom_file)

# --- Add IRQ vector chunk at $FFFE ---
irq_vec = bytes([irq_addr & 0xFF, (irq_addr >> 8) & 0xFF])
rom.add_binary_data(irq_vec, 0xFFFE)

# --- Write back ---
with open(rom_file, 'wb') as f:
    f.write(f"#!RP6502\r\n".encode('ascii'))
    chunks = b""
    addr, data = rom.next_rom_data(0)
    while data is not None:
        header = f"${addr:04X} ${len(data):03X} ${binascii.crc32(data):08X}\r\n"
        chunks += header.encode('ascii') + bytes(data)
        addr += len(data)
        addr, data = rom.next_rom_data(addr)
    if chunks:
        f.write(f"#>${len(chunks):08X} ${binascii.crc32(chunks):08X}\r\n".encode('ascii'))
        f.write(chunks)

print(f"[patch_irq_vec] Written IRQ=${irq_addr:04X} to {rom_file}")
