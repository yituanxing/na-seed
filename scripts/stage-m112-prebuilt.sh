#!/bin/sh
# Host-only signed original Alpine application importer for milestone M112.
# Cross-ISA APK verification, dependency resolution and native ELF machine
# validation are owned by the one canonical staging implementation.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: stage-m112-prebuilt.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m112-batch
work="build/external/m112-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"

sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m112-original-elfs.txt "$work/m112-original-signed-elfs.sha256" 15 count M112
echo "[M112 HOST STAGE PASS] $port 15 original signed Alpine 3.23 native ELF applications, no guest compiler"
