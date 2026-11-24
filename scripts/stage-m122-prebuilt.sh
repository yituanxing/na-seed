#!/bin/sh
# Signed Alpine Netpbm ELF, 15 *globally new* distinct program binaries.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
sh scripts/acceptance/check-elf-history.sh M122 scripts/m122-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m122-batch
work="build/external/m122-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for fixture in original.ppm red.ppm blue.ppm black.pbm white.pgm flipped.ppm \
               cropped.ppm inverted.ppm red-channel.pgm red8.ppm red31.ppm \
               red-blue.ppm original.png invalid.ppm; do
    test -s "$root/tests/m122-$fixture" || exit 1
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m122-original-elfs.txt "$work/m122-original-signed-elfs.sha256" 15 unique M122
echo "[M122 HOST STAGE PASS] $port 15 globally new signed Netpbm native ELFs"
