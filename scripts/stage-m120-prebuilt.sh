#!/bin/sh
# Alpine v3.23 signed LibTIFF programs, one original ELF per tested application.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m120-batch
work="build/external/m120-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for fixture in m120-color.ppm m120-inverted.ppm m120-gray.pgm m120-gray.raw; do
    test -s "$root/tests/$fixture" || exit 1
done

sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m120-original-elfs.txt "$work/m120-original-signed-elfs.sha256" 15 unique M120
echo "[M120 HOST STAGE PASS] $port 15 distinct signed original LibTIFF ELFs, host pixel fixtures"
