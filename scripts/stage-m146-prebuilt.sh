#!/bin/sh
# Stage 24 NEW original signed Alpine 3.23 ELFs; host-only apk installation.
# Staging is a provenance gate, not a claim of successful native QEMU execution.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m146-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M146 "$manifest" --count)
[ "$count" -eq 24 ] || { echo "[M146 STAGE FAIL] expected 24 ELF paths, found $count" >&2; exit 1; }

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m146-batch
work="build/external/m146-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in netpbm imagemagick php84 pax-utils scanelf; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M146 STAGE FAIL] missing original signed package $package" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m146-original-signed-elfs.sha256" 24 contained-elf M146
echo "[M146 SIGNED APK STAGE PASS] $port 24 distinct original ELF SHA256"
echo '[M146 QEMU NOT YET ACCEPTED] native guest behavior still requires validation'
