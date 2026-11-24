#!/bin/sh
# One native NaSeed QEMU per ISA; 15 groff programs, no guest build.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m126-prebuilt.sh "$port"
work="build/external/m126-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m126-batch-cases.tsv

log="$logs/m126-batch.clean.log"
if ! sh "$runner"; then
    echo "[M126 BATCH FAIL] $port 15 signed original groff programs" >&2
    if test -s "$log"; then
        grep -aE '^(M126_[A-Z0-9_]+_(PASS|FAIL)|\[M126 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M126 BATCH FAIL] $port no guest serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M126 "$port" scripts/m126-expected-markers.txt "$log" 15 450
echo "[M126 BATCH PASS] $port 15 original signed groff ELF, single QEMU"
