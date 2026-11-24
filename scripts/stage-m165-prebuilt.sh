#!/bin/sh
# 30 independent, previously unadmitted signed Netpbm ELFs on each native ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m165-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M165 "$manifest" --count --distinct)
[ "$count" -eq 30 ] || exit 1
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m165-batch
work="build/external/m165-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed"
for fixture in tests/m165-batch-run.sh tests/m163-color.ppm \
               tests/m163-gray.pgm tests/m163-bw.pbm tests/m163-rgb.txt; do
    test -s "$root/$fixture"
done
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m165-original-signed-elfs.sha256" 30 contained-exec-elf M165
echo "[M165 SIGNED APK PASS] $port 30 new native image ELF SHA256"
