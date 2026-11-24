#!/bin/sh
# Only stage signed Alpine binaries on host; native QEMU semantic tests are separate.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m143-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M143 "$manifest" --count)
[ "$count" -eq 3 ] || exit 1
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m143-batch
root="build/external/m143-batch/$port"
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/lib/apk/db/installed"
sh scripts/acceptance/hash-original-elfs.sh "$root/rootfs" "$manifest" "$root/m143-original-signed-elfs.sha256" 3 contained-file M143
echo "[M143 STAGED] $port three unique signed native Alpine ELFs; QEMU pending"
