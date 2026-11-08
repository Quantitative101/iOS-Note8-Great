#!/system/bin/sh
# Run in TWRP (adb shell sh /tmp/twrp-rails.sh): CPU/GPU rail voltages from the regulator class, and S2MPS17 registers
# 0x00-0x5f through the s2mps17-regs debugfs (writing i2caddr only selects the register to read).
for d in /sys/class/regulator/regulator.*; do
    n=$(cat $d/name)
    if echo "$n" | grep -q -E '^vdd_(cpucl0|cpucl1|mif|int|g3d)$'; then
        echo "$n $(cat $d/microvolts 2>/dev/null) uV"
    fi
done
cd /sys/kernel/debug/s2mps17-regs || exit 1
i=0
while [ $i -lt 96 ]; do
    r=$(printf '0x%02x' $i)
    echo $r > i2caddr
    printf '%s=%s ' $r "$(cat i2cdata)"
    i=$((i + 1))
done
echo
