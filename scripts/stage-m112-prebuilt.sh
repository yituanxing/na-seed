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

count=0
while IFS= read -r guest_elf; do
    case "$guest_elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$guest_elf"
    count=$((count + 1))
done < scripts/m112-original-elfs.txt > "$work/m112-original-signed-elfs.sha256"

if [ "$count" -ne 15 ] || ! test -s "$work/m112-original-signed-elfs.sha256"; then
    echo "[M112 HOST STAGE FAIL] $port expected 15 original native ELF fingerprints, got $count" >&2
    exit 1
fi
echo "[M112 HOST STAGE PASS] $port 15 original signed Alpine 3.23 native ELF applications, no guest compiler"
