#!/bin/bash
# Two timed boots (phone in TWRP, the Note 8 only) with the drop package under test (now: gestures.off).
# second shows that the system still comes up with it there.
set -eu
cd "$(dirname "$0")/.."
: "${ANDROID_SERIAL:?set ANDROID_SERIAL to your phone}"
[ "$(adb get-state)" = recovery ]
[ "$(adb shell getprop ro.product.device | tr -d '\r')" = greatlte ]
E=$(mktemp)
python3 tools/drop.py adb autorun=autorun/autorun_v14 dark="$E" gestures.off="$E" acpm/dvfs.fw=research/twrp/acpm_dvfs_fw.bin \
    disabled.list=autorun/disabled.list lib/libherosb.dylib=wallpaper/libherosb.dylib lib/libheroapp.dylib=heroapp/libheroapp.dylib \
    wallpaper.png=wallpaper/wallpaper-blue.png wifi/fw.bin=wifi-drop/fw.bin wifi/nvram.txt=wifi-drop/nvram.txt \
    wifi/clm.blob=wifi-drop/clm.blob wifi/network.txt=wifi-drop/network.txt | tail -1
tools/autotest.sh xnu-boot-v131t.img logs/boot-app5.txt | tail -1
tools/autotest.sh xnu-boot-v131t.img logs/boot-app6.txt | tail -1
for f in app5 app6; do
    echo "== $f"
    grep -a -E "/autorun/|HeroReddit" "logs/boot-$f.txt" | cut -c1-150 | head -20 || true
    echo "herosb rounds $(grep -a -c 'herosb: round' "logs/boot-$f.txt" || true), panics $(grep -a -c 'panic(' "logs/boot-$f.txt" || true)"
done
