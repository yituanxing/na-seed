#!/bin/sh
# Test actual 15 signed Linux programs in one QEMU boot; no guest building.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m120-prebuilt.sh "$port"
work="build/external/m120-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m120-batch-cases.tsv

log="$logs/m120-batch.clean.log"
if ! sh "$runner"; then
    echo "[M120 BATCH FAIL] $port native signed LibTIFF functional suite" >&2
    if test -s "$log"; then
        grep -aE '^(M120_[A-Z0-9_]+_(PASS|FAIL)|\[M120 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M120 BATCH FAIL] $port no guest serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M120 "$port" scripts/m120-expected-markers.txt "$log" 15 450
echo "[M120 BATCH PASS] $port 15 independent signed LibTIFF ELFs, QEMU, no guest compilation"
