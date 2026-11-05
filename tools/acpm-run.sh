#!/bin/bash
# Unattended ACPM experiment: tools/acpm-run.sh KERNEL NAME "extra boot args" ...  (phone in TWRP)
# Each NAME/args pair becomes a timed image (greatlte_recovery_after=200) booted with tools/autotest.sh; prints the
# ACPM lines of each log.
set -eu
cd "$(dirname "$0")/.."
K=$1
shift
BASE="$(strings payload110.bin | grep -m1 'debug=0xc')"
while [ $# -ge 2 ]; do
    N=$1
    A=$2
    shift 2
    python3 tools/mkpayload.py "$K" greatlte-i8t.adt "payload-$N.bin" --cmdline "$BASE greatlte_recovery_after=200 $A" >/dev/null
    python3 herolte-pack-boot.py pack ../ref/note8-backup/BOOT.img loader/out/xnu_boot.Image "xnu-boot-$N.img" \
        --append "payload-$N.bin" --partition-size 41943040 >/dev/null
    tools/autotest.sh "xnu-boot-$N.img" "logs/boot-$N.txt" | tail -1
    echo "== $N ($A)"
    grep -a -E "^ACPM: (set|log set|log\[|ACS|CLK)" "logs/boot-$N.txt" | sed -n '/set id/,$p' | head -30
done
