#!/bin/sh
# Six signed software families, 15 new original ELF programs on each ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/acceptance/check-elf-history.sh M135 scripts/m135-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m135-batch
work="build/external/m135-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for pkg in cmake libjpeg-turbo-utils tidyhtml gdbm-tools graphviz netpbm; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed" || {
        echo "[M135 HOST STAGE FAIL] $port unsigned/missing $pkg" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m135-original-elfs.txt "$work/m135-original-signed-elfs.sha256" 15 alias-exec M135
echo "[M135 HOST STAGE PASS] $port 15 newly signed and distinct ELF files"
