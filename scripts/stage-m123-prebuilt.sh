#!/bin/sh
# Signed Alpine ext2/FAT original ELF programs; host-only APK install.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

sh scripts/acceptance/check-elf-history.sh M123 scripts/m123-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m123-batch
work="build/external/m123-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for img in ext2 empty-ext empty-fat; do
    test -s "$root/tests/m123-$img.img" || exit 1
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m123-original-elfs.txt "$work/m123-original-signed-elfs.sha256" 15 unique M123
echo "[M123 HOST STAGE PASS] $port 15 globally new native ext2/FAT original ELFs"
