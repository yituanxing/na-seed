#!/bin/sh
# Run 15 original elfutils Linux ELF programs in ONE native NaSeed QEMU boot.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: probe-m118-batch.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-alpine-signed-batch.sh "$port" m118
work="build/external/m118-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m118-batch-cases.tsv

log="$logs/m118-batch.clean.log"
if ! sh "$runner"; then
    echo "[M118 BATCH FAIL] $port signed original ELF/DWARF functional suite" >&2
    if test -s "$log"; then
        grep -aE '^(M118_[A-Z0-9_]+_(PASS|FAIL)|\[M118 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || {
    echo "[M118 BATCH FAIL] $port no actual guest QEMU serial" >&2
    exit 1
}
manifest=scripts/m118-expected-markers.txt
test -s "$manifest"
sh scripts/acceptance/check-batch-markers.sh M118 "$port" "$manifest" "$log" 15 450
echo "[M118 BATCH PASS] $port 15 independent signed elfutils ELFs, one guest boot, no guest build"
