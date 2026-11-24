#!/bin/sh
# One real QEMU boot per ISA, 15 independently attested original ELF cases.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m131-prebuilt.sh "$port"
work="build/external/m131-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m131-batch-cases.tsv

log="$logs/m131-batch.clean.log"
if ! sh "$runner"; then
    echo "[M131 BATCH FAIL] $port 15 original Netpbm ELF tools" >&2
    if test -s "$log"; then
        grep -aE '^(M131_[A-Z0-9_]+_(PASS|FAIL)|\[M131 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M131 BATCH FAIL] $port missing serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M131 "$port" scripts/m131-expected-markers.txt "$log" 15 450
echo "[M131 BATCH PASS] $port 15 signed original Alpine ELFs, real QEMU"
