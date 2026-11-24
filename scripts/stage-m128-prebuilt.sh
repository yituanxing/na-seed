#!/bin/sh
# Signed Alpine filesystem image tools, host APK installation only.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

# The expanding catalog is a collection of *different* original programs.
sh scripts/acceptance/check-elf-history.sh M128 scripts/m128-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m128-batch
work="build/external/m128-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in exfatprogs squashfs-tools squashfs-tools-ng blkid wipefs; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M128 HOST STAGE FAIL] $port signed APK missing $package" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m128-original-elfs.txt "$work/m128-original-signed-elfs.sha256" 15 alias-path M128
echo "[M128 HOST STAGE PASS] $port 15 globally new signed filesystem ELF programs"
