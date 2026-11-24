#!/bin/sh
# Third original-app batch: host-only, signature-verified Alpine APK overlay.
# NaSeed receives the original target ELF bytes and executes them unchanged.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: stage-m110-prebuilt.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m110-batch
work="build/external/m110-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m110-original-elfs.txt "$work/m110-original-signed-elfs.sha256" 21 count M110
echo "[M110 HOST STAGE PASS] $port 20 different signed Alpine original Linux applications, 21 ELFs, no guest compiler"
