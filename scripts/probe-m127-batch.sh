#!/bin/sh
# One native NaSeed QEMU per ISA; 15 audio codec programs, no guest build.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m127-prebuilt.sh "$port"
work="build/external/m127-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m127-batch-cases.tsv

log="$logs/m127-batch.clean.log"
if ! sh "$runner"; then
    echo "[M127 BATCH FAIL] $port 15 signed original audio codec programs" >&2
    if test -s "$log"; then
        grep -aE '^(M127_[A-Z0-9_]+_(PASS|FAIL)|\[M127 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M127 BATCH FAIL] $port no guest serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M127 "$port" scripts/m127-expected-markers.txt "$log" 15 450
echo "[M127 BATCH PASS] $port 15 original signed audio codec ELF, single QEMU"
