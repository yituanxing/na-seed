#!/bin/sh
# Signed Alpine Netpbm ELF, 15 *globally new* distinct program binaries.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for manifest in scripts/m[0-9]*-original-elfs.txt; do
        [ "$manifest" = scripts/m122-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$manifest"; then
            echo "[M122 DUPLICATE ORIGINAL ELF] $elf already in $manifest" >&2
            exit 1
        fi
    done
done < scripts/m122-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m122-batch
work="build/external/m122-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for fixture in original.ppm red.ppm blue.ppm black.pbm white.pgm flipped.ppm \
               cropped.ppm inverted.ppm red-channel.pgm red8.ppm red31.ppm \
               red-blue.ppm original.png invalid.ppm; do
    test -s "$root/tests/m122-$fixture" || exit 1
done
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$elf"
    count=$((count + 1))
done < scripts/m122-original-elfs.txt > "$work/m122-original-signed-elfs.sha256"
unique=$(awk '{ print $1 }' "$work/m122-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M122 HOST STAGE FAIL] $port $count programs, $unique unique ELF SHA256" >&2
    exit 1
fi
echo "[M122 HOST STAGE PASS] $port 15 globally new signed Netpbm native ELFs"
