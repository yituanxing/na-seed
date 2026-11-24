#!/bin/sh
# One native NaSeed QEMU guest per ISA: 15 original filesystem ELF tools.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m128-prebuilt.sh "$port"
work="build/external/m128-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m128-batch-cases.tsv

log="$logs/m128-batch.clean.log"
if ! sh "$runner"; then
    echo "[M128 BATCH FAIL] $port 15 signed original filesystem programs" >&2
    if test -s "$log"; then
        grep -aE '^(M128_[A-Z0-9_]+_(PASS|FAIL)|\[M128 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M128 BATCH FAIL] $port no guest serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M128 "$port" scripts/m128-expected-markers.txt "$log" 15 450
echo "[M128 BATCH PASS] $port 15 original signed filesystem ELF, single QEMU"
