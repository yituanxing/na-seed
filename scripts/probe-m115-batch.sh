#!/bin/sh
# Run all fifteen original Alpine M115 applications inside one NaSeed QEMU.
# The Linux host installs the signed APKs, NaSeed only runs real programs.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: probe-m115-batch.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m115-prebuilt.sh "$port"
work="build/external/m115-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m115-batch-cases.tsv

log="$logs/m115-batch.clean.log"
if ! sh "$runner"; then
    echo "[M115 BATCH FAIL] $port original signed Alpine Linux ELF runtime" >&2
    if test -s "$log"; then
        grep -aE '^(M115_[A-Z0-9_]+_(PASS|FAIL)|\[M115 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || {
    echo "[M115 BATCH FAIL] $port missing authentic guest QEMU serial" >&2
    exit 1
}
manifest=scripts/m115-expected-markers.txt
test -s "$manifest"
sh scripts/acceptance/check-batch-markers.sh M115 "$port" "$manifest" "$log" 15 450
echo "[M115 BATCH PASS] $port 15 signed original Linux applications, one QEMU boot, host-prebuilt"
