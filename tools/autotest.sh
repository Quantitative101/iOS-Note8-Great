#!/bin/bash
# One unattended test cycle on the Note 8 (phone in TWRP): flash a BOOT image with readback verification, boot it, wait
# for its greatlte_recovery_after reset back into TWRP, pull the kernel log.
#
#   tools/autotest.sh xnu-boot-vNN.img logs/NAME.txt
#
# The image must have greatlte_recovery_after set, or this waits until someone brings the phone back to TWRP.
set -eu
cd "$(dirname "$0")/.."
IMG=$1
LOG=$2
SIZE=$(stat -f%z "$IMG")
WANT=$(shasum -a 256 "$IMG" | cut -d' ' -f1)
adb wait-for-recovery
adb push "$IMG" /tmp/boot.img >/dev/null
GOT=$(adb shell "dd if=/tmp/boot.img of=/dev/block/sda7 bs=1M 2>/dev/null && sync && head -c $SIZE /dev/block/sda7 | sha256sum" | cut -d' ' -f1)
if [ "$GOT" != "$WANT" ]; then
    echo "BOOT readback $GOT does not match $WANT: not booting" >&2
    exit 1
fi
echo "$(date +%H:%M:%S) flashed $IMG ($WANT), booting"
adb reboot
sleep 20
adb wait-for-recovery
sleep 3
python3 tools/log-pull.py adb "$LOG" | tail -1
echo "$(date +%H:%M:%S) back in TWRP, log in $LOG"
