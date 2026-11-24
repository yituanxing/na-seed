#!/bin/sh
# M141: host-only signed APK staging of 68 distinct original Alpine ELFs.
# This counts no guest PASS; QEMU semantic probes are a separate mandatory gate.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

manifest=scripts/m141-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M141 "$manifest" --count)
[ "$count" -eq 68 ] || { echo "[M141 STAGE FAIL] $count paths, expected 68" >&2; exit 1; }

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m141-batch
work="build/external/m141-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m141-original-signed-elfs.sha256" 68 contained-file M141
echo "[M141 SIGNED APK STAGE PASS] $port 68 different original target ELFs"
echo '[M141 NOT ACCEPTED] native QEMU semantic checks still required'
