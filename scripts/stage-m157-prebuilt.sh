#!/bin/sh
# M157: 30 original signed and historically new Alpine image utilities.
# This host import uses exactly the shared APK signature/ISA authority.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m157-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M157 "$manifest" --count --distinct)
[ "$count" -eq 30 ] || {
    echo "[M157 INVENTORY FAIL] need 30 native ELF paths, got $count" >&2
    exit 1
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m157-batch
work="build/external/m157-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in netpbm giflib-utils tiff-tools libpng-utils; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M157 STAGE FAIL] unsigned/missing package $port $package" >&2
        exit 1
    }
done
for fixture in m157-color.ppm m157-bw.pbm m157-gray.pgm m157-color.png \
               m157-bad-itxt.png m157-color.gif m157-paletted.tif m157-fax.raw; do
    test -s "$root/tests/$fixture" || {
        echo "[M157 STAGE FAIL] missing input $fixture" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m157-original-signed-elfs.sha256" 30 contained-elf M157
echo "[M157 APK STAGE PASS] $port 30 distinct original signed Alpine ELF hashes"
echo '[M157 GUEST SEMANTICS PENDING] native QEMU acceptance is a separate gate'
