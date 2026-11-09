#!/bin/bash
# Two timed profiled boots (phone in TWRP, the Note 8 only) with libherosb removing the vibrancy filters; the frame
# rates, herosb's lines and the raw profile of the second boot go to the log.
set -eu
cd "$(dirname "$0")/.."
: "${ANDROID_SERIAL:?set ANDROID_SERIAL to your phone}"
[ "$(adb get-state)" = recovery ]
[ "$(adb shell getprop ro.product.device | tr -d '\r')" = greatlte ]
E=$(mktemp)
python3 tools/drop.py adb autorun=autorun/autorun_v12 dark="$E" novibrant="$E" acpm/dvfs.fw=research/twrp/acpm_dvfs_fw.bin \
    disabled.list=autorun/disabled.list lib/libherosb.dylib=wallpaper/libherosb.dylib lib/libheroapp.dylib=heroapp/libheroapp.dylib \
    wallpaper.png=wallpaper/wallpaper.png wifi/fw.bin=wifi-drop/fw.bin wifi/nvram.txt=wifi-drop/nvram.txt \
    wifi/clm.blob=wifi-drop/clm.blob wifi/network.txt=wifi-drop/network.txt | tail -1
tools/autotest.sh xnu-boot-f132.img logs/boot-g132a.txt | tail -1
tools/autotest.sh xnu-boot-f132.img logs/boot-g132b.txt | tail -1
grep -a -E "herosb: .*vibran" logs/boot-g132b.txt | cut -c1-120 || true
grep -a -c "panic(" logs/boot-g132b.txt || true
grep -a "PCPROF: .* user" logs/boot-g132b.txt | tail -3
grep -a PCSTACK logs/boot-g132b.txt | cut -c1-60 || true
