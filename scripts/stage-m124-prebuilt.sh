#!/bin/sh
# Signed Alpine util-linux original ELF programs; host-only APK install.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

sh scripts/acceptance/check-elf-history.sh M124 scripts/m124-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m124-batch
work="build/external/m124-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:util-linux-misc' "$root/lib/apk/db/installed"
test -s "$root/tests/m124-empty-minix.img"
test -s "$root/tests/m124-empty-swap.img"
test -s "$root/tests/m124-cramfs-source/hello.txt"
test -s "$root/tests/m124-look-words.txt"
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m124-original-elfs.txt "$work/m124-original-signed-elfs.sha256" 15 alias-path M124
echo "[M124 HOST STAGE PASS] $port 15 globally new native util-linux original ELFs"
