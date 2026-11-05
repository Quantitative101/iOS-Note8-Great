#!/bin/bash
# Does SpringBoard still reach its home screen with these launchd jobs disabled? (phone in TWRP, unattended)
#
#   tools/service-test.sh IMAGE NAME label [label ...]
#
# Writes the labels as the drop zone's disabled.list (with the rest of the usual drop package), boots IMAGE twice with
# tools/autotest.sh (the autorun writes launchd's disabled lists during the first boot; they apply from the second) and
# looks in the second boot's log for libherosb's "round N, M windows, picture 1" line, which SpringBoard's main thread
# prints only once its windows are up. Prints PASS or FAIL. IMAGE must have greatlte_recovery_after set.
set -eu
cd "$(dirname "$0")/.."
IMG=$1
NAME=$2
shift 2
LIST=autorun/disabled.test
{
    echo "# service test $NAME ($(date +%H:%M))"
    for l in "$@"; do echo "$l"; done
} > "$LIST"
python3 tools/drop.py adb autorun=autorun/autorun_v3 disabled.list="$LIST" lib/libherosb.dylib=wallpaper/libherosb.dylib \
    lib/libheroapp.dylib=heroapp/libheroapp.dylib wallpaper.png=wallpaper/wallpaper.png wifi/fw.bin=wifi-drop/fw.bin \
    wifi/nvram.txt=wifi-drop/nvram.txt wifi/clm.blob=wifi-drop/clm.blob wifi/network.txt=wifi-drop/network.txt | tail -1
tools/autotest.sh "$IMG" "logs/svc-$NAME-1.txt" | tail -1
tools/autotest.sh "$IMG" "logs/svc-$NAME-2.txt" | tail -1
if grep -a -q "herosb: round [0-9]*, [0-9]* windows, picture 1" "logs/svc-$NAME-2.txt"; then
    echo "$NAME PASS ($*)"
else
    echo "$NAME FAIL ($*)"
fi
