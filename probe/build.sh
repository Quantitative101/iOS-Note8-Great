#!/bin/sh
# Build out/probe.Image for the Galaxy Note 8 (SM-N950F). No packaging or flashing.
# Uses Apple clang (cross-compiles) + Homebrew lld + llvm-objcopy.
set -eu
cd "$(dirname "$0")"
LLVM=/opt/homebrew/opt/llvm/bin
mkdir -p out
CFLAGS="-target aarch64-none-elf -c -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -nostdlib -fno-asynchronous-unwind-tables -fno-jump-tables -mgeneral-regs-only -mstrict-align -mcpu=cortex-a53 -Wall -Wextra -Werror"
objs=""
for src in probe.S probe.c fbtext.c ../fdt.c; do
    obj="out/probe_$(basename "$src" | tr . _).o"
    clang $CFLAGS "$src" -o "$obj"
    objs="$objs $obj"
done
ld.lld -static -T image.ld --build-id=none $objs -o out/probe.elf
if $LLVM/llvm-readelf -r $objs | grep -q 'ABS64'; then
    echo "probe: absolute relocation found" >&2
    exit 1
fi
$LLVM/llvm-objcopy -O binary out/probe.elf out/probe.Image
ls -l out/probe.Image
xxd -s 56 -l 4 out/probe.Image
