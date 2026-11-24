#!/bin/sh
# Host-only signed original ELF staging for the M114 fifteen-program batch.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: stage-m114-prebuilt.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# One canonical installer/validator owns signatures, dependencies, machine
# checks and initramfs assembly. No install/build step runs inside NaSeed.
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m114-batch
work="build/external/m114-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"

sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m114-original-elfs.txt "$work/m114-original-signed-elfs.sha256" 15 count M114
echo "[M114 HOST STAGE PASS] $port 15 original signed Alpine 3.23 native ELF applications, zero guest compilation"
