#!/bin/sh
# M140: signed Alpine GNU multicall plus nineteen distinct Netpbm ELFs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# New means historically uncounted, not a BusyBox alias or duplicate SHA.
sh scripts/acceptance/check-elf-history.sh M140 scripts/m140-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m140-batch
work="build/external/m140-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:coreutils' "$root/lib/apk/db/installed" || exit 1
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed" || exit 1
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m140-original-elfs.txt "$work/m140-original-signed-elfs.sha256" 20 contained-exec M140
echo "[M140 HOST STAGE PASS] $port 20 distinct signed GNU/Netpbm ELFs"
