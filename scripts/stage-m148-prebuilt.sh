#!/bin/sh
# 35 distinct ORIGINAL Alpine 3.23 native Netpbm ELFs, host-only signed staging.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m148-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M148 "$manifest" --count)
[ "$count" -eq 35 ] || { echo "[M148 STAGE FAIL] 35 expected ELF paths, got $count" >&2; exit 1; }

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m148-batch
work="build/external/m148-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed" || exit 1
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m148-original-signed-elfs.sha256" 35 contained-elf M148
echo "[M148 SIGNED APK STAGE PASS] $port 35 original distinct ELF SHA256"
echo '[M148 NOT QEMU ACCEPTED] Native guest semantic jobs still required'
