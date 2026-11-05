#!/bin/bash
# Gives the Note 8's Wi-Fi the networks to join: asks for each name and passphrase here, in the terminal, so they are
# typed only by you, writes them to note8/wifi-drop/network.txt (name, passphrase, name, passphrase ...; never printed or
# committed), and puts the whole Wi-Fi drop package on the phone. Run it with the phone in TWRP, as yourself (no sudo),
# then boot it. At boot the kernel joins the listed network the first scan hears loudest (the first one if none is in range).
#
#   tools/wifi-set-network.sh           # asks for networks one after another
#   tools/wifi-set-network.sh clear     # removes them all (iOS chooses)
#   tools/wifi-set-network.sh keep      # leaves network.txt as it is and just drops everything again
set -eu
cd "$(dirname "$0")/.."
FILE=wifi-drop/network.txt
MODE="${1:-ask}"
if [ "$MODE" = "clear" ]; then
    rm -f "$FILE"
    echo "network.txt removed from wifi-drop; the next drop has no network"
elif [ "$MODE" = "ask" ]; then
    umask 077
    rm -f "$FILE"       # an older copy (made by a run under sudo) may be owned by root: removing it only needs this folder
    COUNT=0
    while true; do
        printf "Network name (SSID)%s: " "$([ $COUNT -gt 0 ] && echo ' (empty line = done)')"
        read -r SSID
        if [ -z "$SSID" ]; then
            [ $COUNT -gt 0 ] && break
            echo "a name is needed for the first network" >&2
            continue
        fi
        printf "Passphrase for \"%s\" (typing is hidden): " "$SSID"
        read -rs PASS
        echo
        if [ ${#SSID} -gt 32 ] || [ ${#PASS} -lt 8 ] || [ ${#PASS} -gt 63 ]; then
            echo "a name of 1-32 characters and a passphrase of 8-63 are needed; try that network again" >&2
            continue
        fi
        printf '%s\n%s\n' "$SSID" "$PASS" >> "$FILE"
        COUNT=$((COUNT + 1))
        echo "  saved network $COUNT"
    done
    echo "$COUNT network(s) written to $FILE"
fi
ITEMS=(autorun=autorun/autorun_v3 disabled.list=autorun/disabled.list lib/libherosb.dylib=wallpaper/libherosb.dylib
       lib/libheroapp.dylib=heroapp/libheroapp.dylib wallpaper.png=wallpaper/wallpaper.png
       wifi/fw.bin=wifi-drop/fw.bin wifi/nvram.txt=wifi-drop/nvram.txt wifi/clm.blob=wifi-drop/clm.blob)
[ -f "$FILE" ] && ITEMS+=(wifi/network.txt="$FILE")
python3 tools/drop.py adb "${ITEMS[@]}"
