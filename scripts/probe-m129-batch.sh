#!/bin/sh
# Whole 15-program suite in a single real system-QEMU NaSeed guest per ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m129-prebuilt.sh "$port"
work="build/external/m129-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m129-batch-cases.tsv

log="$logs/m129-batch.clean.log"
if ! sh "$runner"; then
    echo "[M129 BATCH FAIL] $port 15 original process/monitoring ELFs" >&2
    if test -s "$log"; then
        grep -aE '^(M129_[A-Z0-9_]+_(PASS|FAIL)|\[M129 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M129 BATCH FAIL] $port no serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M129 "$port" scripts/m129-expected-markers.txt "$log" 15 450
echo "[M129 BATCH PASS] $port 15 original signed ELF, single QEMU"
