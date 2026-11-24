#!/bin/sh
# M147 only admits independently signed Alpine APK binaries on the host.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m147-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M147 "$manifest" --count)
[ "$count" -eq 29 ] || { echo "[M147 FAIL] inventory count=$count" >&2; exit 1; }
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m147-batch
root="build/external/m147-batch/$1"
test -s "$root/rootfs.cpio" && test -s "$root/rootfs/lib/apk/db/installed"
sh scripts/acceptance/hash-original-elfs.sh "$root/rootfs" "$manifest" "$root/m147-original-elfs.sha256" 29 contained-file M147
echo '[M147 STAGED] 29 genuinely new signed Alpine ELF identities; QEMU pending'
