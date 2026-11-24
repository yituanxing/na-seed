#!/bin/sh
# M150: 26 genuinely new original signed Alpine v3.23 ELF identities.
# The shared host APK installer verifies Alpine signatures/architecture.
# This gate additionally rejects old milestone paths, BusyBox aliases and
# duplicate actual SHA256 content, never counting symlinks as new software.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m150-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M150 "$manifest" --count --distinct)
[ "$count" -eq 26 ] || {
    echo "[M150 STAGE FAIL] expected 26 new paths, got $count" >&2; exit 1;
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m150-batch
work="build/external/m150-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in lrzip par2cmdline pixz plzip sleuthkit; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M150 STAGE FAIL] missing signed Alpine package $package" >&2
        exit 1
    }
done
for fixture in m150-ext2.img m150-mbr.img m150-input.txt; do
    test -s "$root/tests/$fixture" || exit 1
done
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m150-original-signed-elfs.sha256" 26 contained-elf M150
echo "[M150 SIGNED APK STAGE PASS] $port 26 distinct original ELF SHA256"
echo '[M150 QEMU NOT YET ACCEPTED] guest semantic proof pending native run'
