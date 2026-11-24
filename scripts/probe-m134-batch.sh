#!/bin/sh
# Exact batch of 15 new native Alpine ELF functional oracles per system QEMU.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-alpine-signed-batch.sh "$port" m134
work="build/external/m134-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m134-batch-cases.tsv

log="$logs/m134-batch.clean.log"
if ! sh "$runner"; then
    echo "[M134 BATCH FAIL] $port all 15 signed graph/interpreter/code-generation programs" >&2
    if test -s "$log"; then
        grep -aE '^(M134_[A-Z0-9_]+_(PASS|FAIL)|\[M134 )' "$log" >&2 || true
        tail -500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M134 BATCH FAIL] $port missing serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M134 "$port" scripts/m134-expected-markers.txt "$log" 15 500
echo "[M134 BATCH PASS] $port 15 signed original Alpine ELF tools, real QEMU"
