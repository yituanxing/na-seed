#!/bin/sh
# Run all 15 signed original Linux ELF applications in one NaSeed QEMU boot.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: probe-m117-batch.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m117-prebuilt.sh "$port"
work="build/external/m117-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m117-batch-cases.tsv

log="$logs/m117-batch.clean.log"
if ! sh "$runner"; then
    echo "[M117 BATCH FAIL] $port signed original Alpine Linux ELF runtime" >&2
    if test -s "$log"; then
        grep -aE '^(M117_[A-Z0-9_]+_(PASS|FAIL)|\[M117 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || {
    echo "[M117 BATCH FAIL] $port missing original guest QEMU serial" >&2
    exit 1
}
manifest=scripts/m117-expected-markers.txt
test -s "$manifest"
sh scripts/acceptance/check-batch-markers.sh M117 "$port" "$manifest" "$log" 15 450
echo "[M117 BATCH PASS] $port 15 distinct signed Linux ELFs, one QEMU boot, host-prebuilt"
