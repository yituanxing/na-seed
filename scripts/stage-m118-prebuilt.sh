#!/bin/sh
# Fifteen native original signed elfutils ELFs; debug fixtures HOST-built only.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: stage-m118-prebuilt.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m118-batch
work="build/external/m118-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for artifact in m118-debug.o m118-debug-other.o m118-debug.elf m118-textrel.so; do
    test -s "$root/tests/$artifact" || exit 1
done

sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m118-original-elfs.txt "$work/m118-original-signed-elfs.sha256" 15 unique M118
echo "[M118 HOST STAGE PASS] $port 15 independent signed elfutils native ELFs, host-only -g fixtures"
