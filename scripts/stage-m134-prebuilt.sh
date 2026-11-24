#!/bin/sh
# Eight signed software families, 15 new real binaries on each native ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/acceptance/check-elf-history.sh M134 scripts/m134-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m134-batch
work="build/external/m134-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for pkg in graphviz lua5.4 dash mawk xxd gperf ninja-build fish; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed" || {
        echo "[M134 HOST STAGE FAIL] $port unsigned/missing $pkg" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m134-original-elfs.txt "$work/m134-original-signed-elfs.sha256" 15 alias-exec M134
echo "[M134 HOST STAGE PASS] $port 15 newly signed and distinct ELF files"
