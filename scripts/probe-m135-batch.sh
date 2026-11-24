#!/bin/sh
# Exact batch of 15 new native Alpine ELF functional oracles per system QEMU.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m135-prebuilt.sh "$port"
work="build/external/m135-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m135-batch-cases.tsv

log="$logs/m135-batch.clean.log"
if ! sh "$runner"; then
    echo "[M135 BATCH FAIL] $port all 15 signed build/JPEG/GDBM/graph/Netpbm original programs" >&2
    if test -s "$log"; then
        grep -aE '^(M135_[A-Z0-9_]+_(PASS|FAIL)|\[M135 )' "$log" >&2 || true
        tail -500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M135 BATCH FAIL] $port missing serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M135 "$port" scripts/m135-expected-markers.txt "$log" 15 500
echo "[M135 BATCH PASS] $port 15 signed original Alpine ELF tools, real QEMU"
