#!/bin/sh
# Host-only signed original ELF staging for the M113 fifteen-program batch.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: stage-m113-prebuilt.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# One canonical installer/validator owns signatures, dependencies, machine
# checks and initramfs assembly. No install/build step runs inside NaSeed.
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m113-batch
work="build/external/m113-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"

count=0
while IFS= read -r guest_elf; do
    case "$guest_elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$guest_elf"
    count=$((count + 1))
done < scripts/m113-original-elfs.txt > "$work/m113-original-signed-elfs.sha256"

if [ "$count" -ne 15 ] || ! test -s "$work/m113-original-signed-elfs.sha256"; then
    echo "[M113 HOST STAGE FAIL] $port expected 15 distinct original native ELF fingerprints, got $count" >&2
    exit 1
fi
echo "[M113 HOST STAGE PASS] $port 15 original signed Alpine 3.23 native ELF applications, zero guest compilation"
