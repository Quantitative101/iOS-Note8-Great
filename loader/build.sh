#!/bin/sh
# Build out/xnu_boot.Image (XNU loader) for the Galaxy Note 8 (SM-N950F). No packaging or flashing.
set -eu
cd "$(dirname "$0")"
LLVM=/opt/homebrew/opt/llvm/bin
mkdir -p out
CFLAGS="-target aarch64-none-elf -c -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -nostdlib -fno-asynchronous-unwind-tables -fno-jump-tables -mgeneral-regs-only -mstrict-align -mcpu=cortex-a53 -Wall -Wextra -Werror"
objs=""
for src in xnu_boot.S xnu_boot.c fbtext.c ../fdt.c; do
    obj="out/xnu_boot_$(basename "$src" | tr . _).o"
    clang $CFLAGS "$src" -o "$obj"
    objs="$objs $obj"
done
ld.lld -static -T image.ld --build-id=none $objs -o out/xnu_boot.elf
if $LLVM/llvm-readelf -r $objs | grep -q 'ABS64'; then
    echo "xnu_boot: absolute relocation found" >&2
    exit 1
fi
$LLVM/llvm-objcopy -O binary out/xnu_boot.elf out/xnu_boot.Image
ls -l out/xnu_boot.Image
xxd -s 56 -l 4 out/xnu_boot.Image
end=$($LLVM/llvm-nm out/xnu_boot.elf | awk '$3 == "_image_end" { print $1 }')
echo "_image_end=0x$end"
