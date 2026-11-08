#!/bin/bash
# Symbolize a boot log's PCPROF lines (greatlte_pcprof) against the iOS root's shared cache: per-function sample counts.
#   tools/pcprof-sym.sh logs/boot-X.txt [shared cache path]
set -eu
LOG=$1
C=${2:-/private/tmp/g26/rootmnt/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64}
grep -a -E "^PCPROF: [0-9]+ 0x" "$LOG" | awk '{c[$3]+=$2} END{for(k in c) print c[k], k}' | sort -rn | head -150 |
    while read -r n a; do
        s=$(ipsw dyld a2s "$C" "$a" 2>/dev/null | tail -1 | sed -E 's/^0x[0-9a-f]+: //; s/ \+ [0-9]+$//')
        echo "$n $s"
    done | awk '{n=$1; $1=""; c[$0]+=n; t+=n} END{for(k in c) printf "%5.1f%% %d%s\n", c[k]*100/t, c[k], k; }' | sort -rn | head -12 | c++filt
grep -a "PCPROF: .* user" "$LOG" | awk '{u+=$4; k+=$6} END{print "samples in user", u, "kernel", k}'
