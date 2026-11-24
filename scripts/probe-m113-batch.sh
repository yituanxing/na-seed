#!/bin/sh
# Run all fifteen ORIGINAL M113 Alpine ELFs inside a SINGLE NaSeed QEMU boot.
# Linux host installs all APKs and passes the signed ELF initramfs to the guest.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: probe-m113-batch.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m113-prebuilt.sh "$port"
work="build/external/m113-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m113-batch-cases.tsv

log="$logs/m113-batch.clean.log"
if ! sh "$runner"; then
    echo "[M113 BATCH FAIL] $port real signed Alpine original Linux ELF runtime" >&2
    if test -s "$log"; then
        grep -aE '^(M113_[A-Z0-9_]+_(PASS|FAIL)|\[M113 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || {
    echo "[M113 BATCH FAIL] $port missing actual guest serial" >&2
    exit 1
}
manifest=scripts/m113-expected-markers.txt
test -s "$manifest"
sh scripts/acceptance/check-batch-markers.sh M113 "$port" "$manifest" "$log" 15 450
echo "[M113 BATCH PASS] $port 15 signed original Linux applications, one QEMU boot, host-prebuilt"
