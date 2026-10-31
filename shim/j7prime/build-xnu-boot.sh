#!/bin/sh
# Build the Galaxy J7 Prime S-Boot loader. No flash or payload packaging here.
set -eu
cd "$(dirname "$0")"
mkdir -p outxnu
: "${CC:=gcc}"
: "${LD:=ld}"
: "${OBJCOPY:=objcopy}"
CFLAGS="-c -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -nostdlib -fno-asynchronous-unwind-tables -mgeneral-regs-only -mstrict-align -march=armv8-a -Wall -Wextra -Werror"
"${CC}" $CFLAGS xnu_boot.S -o outxnu/xnu_boot.o
"${CC}" $CFLAGS xnu_boot.c -o outxnu/xnu_boot_c.o
"${CC}" $CFLAGS display_console.c -o outxnu/display_console.o
"${CC}" $CFLAGS charging.c -o outxnu/charging.o
"${CC}" $CFLAGS ../fdt.c -o outxnu/fdt.o
"${LD}" -static -T xnu_boot.ld --build-id=none outxnu/xnu_boot.o outxnu/xnu_boot_c.o outxnu/display_console.o outxnu/charging.o outxnu/fdt.o -o outxnu/xnu_boot.elf
"${OBJCOPY}" -O binary outxnu/xnu_boot.elf outxnu/Image
python3 - <<'PY'
from pathlib import Path
import struct
p = Path('outxnu/Image').read_bytes()
assert len(p) <= 0x4000, f'loader ({len(p)} bytes) overlaps embedded payload offset 0x4000'
assert p[56:60] == b'ARMd'
assert struct.unpack_from('<Q', p, 8)[0] == 0
assert struct.unpack_from('<Q', p, 16)[0] > len(p)
assert struct.unpack_from('<Q', p, 24)[0] == 0xa
print(f'outxnu/Image: {len(p)} bytes; arm64 Image header OK')
PY
