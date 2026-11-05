#!/bin/bash
# Samples the Mac's USB bus every 5 s into logs/usb-sampler.txt, to see whether the Note 8 shows up as a device while
# the DWC3 test image runs. Usage: tools/usb-sampler.sh [count]
OUT="$(dirname "$0")/../logs/usb-sampler.txt"
for i in $(seq 1 "${1:-130}"); do
    echo "== $(date +%H:%M:%S)" >> "$OUT"
    ioreg -p IOUSB -w0 | grep -v "AppleT6000USBXHCI\|Root" | sed 's/^[ |+o-]*//' >> "$OUT"
    sleep 5
done
