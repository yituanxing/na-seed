#!/bin/sh
# M159: one signed Alpine APK overlay with 8 SVN + 5 NSS + 17 Netpbm ELFs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m159-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M159 "$manifest" --count --distinct)
[ "$count" -eq 30 ] || {
    echo "[M159 INVENTORY FAIL] require exactly 30 new native ELF paths, got $count" >&2
    exit 1
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m159-batch
work="build/external/m159-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in subversion nss-tools netpbm; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M159 APK FAIL] $port original Alpine $package not installed" >&2
        exit 1
    }
done
for input in m159-color.ppm m159-gray.pgm m159-bw.pbm \
             m159-multi.pnm m159-rgb.txt m159-svn-note.txt m159-test-cert.p12; do
    test -s "$root/tests/$input" || exit 1
done
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m159-original-signed-elfs.sha256" 30 contained-elf M159
echo "[M159 APK STAGE PASS] $port 30 distinct signed native ELF SHA256 (SVN/NSS/image)"
echo '[M159 REAL APPLICATION EXECUTION PENDING] native three-ISA QEMU is a separate gate'
