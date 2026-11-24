#!/bin/sh
# Signed Alpine Netpbm original ELF programs; host-only APK install.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

sh scripts/acceptance/check-elf-history.sh M125 scripts/m125-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m125-batch
work="build/external/m125-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed"
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m125-original-elfs.txt "$work/m125-original-signed-elfs.sha256" 15 alias-path M125
echo "[M125 HOST STAGE PASS] $port 15 globally new native Netpbm original ELFs"
