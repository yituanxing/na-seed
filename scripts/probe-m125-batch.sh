#!/bin/sh
# One native NaSeed QEMU per ISA; 15 Netpbm programs, no guest build.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-alpine-signed-batch.sh "$port" m125
work="build/external/m125-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m125-batch-cases.tsv

log="$logs/m125-batch.clean.log"
if ! sh "$runner"; then
    echo "[M125 BATCH FAIL] $port 15 signed original Netpbm programs" >&2
    if test -s "$log"; then
        grep -aE '^(M125_[A-Z0-9_]+_(PASS|FAIL)|\[M125 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M125 BATCH FAIL] $port no guest serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M125 "$port" scripts/m125-expected-markers.txt "$log" 15 450
echo "[M125 BATCH PASS] $port 15 original signed Netpbm ELF, single QEMU"
