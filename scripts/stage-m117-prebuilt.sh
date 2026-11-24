#!/bin/sh
# Host-stage 15 independent signed Alpine native ELFs; no guest compilation.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: stage-m117-prebuilt.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# Canonical overlay resolves and signature-verifies APKs on Linux host,
# enforces correct ELF machine, then packs the guest initramfs.
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m117-batch
work="build/external/m117-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"

sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m117-original-elfs.txt "$work/m117-original-signed-elfs.sha256" 15 unique M117
echo "[M117 HOST STAGE PASS] $port 15 distinct signed Alpine 3.23 native ELFs; zero guest builds"
