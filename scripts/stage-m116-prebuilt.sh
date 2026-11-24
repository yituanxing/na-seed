#!/bin/sh
# M116: host-stage fifteen unmodified signed Alpine 3.23 native ELF programs.
# NaSeed only executes already-built binaries and verifies real output data.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: stage-m116-prebuilt.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# The canonical installer verifies the Alpine signature, dependencies, ELF
# machine type and full manifest while constructing one host-side initramfs.
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m116-batch
work="build/external/m116-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"

sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m116-original-elfs.txt "$work/m116-original-signed-elfs.sha256" 15 unique M116
echo "[M116 HOST STAGE PASS] $port 15 original signed Alpine 3.23 native ELF programs, zero guest compilation"
