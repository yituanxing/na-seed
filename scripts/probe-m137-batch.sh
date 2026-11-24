#!/bin/sh
# Exact batch of 20 new native Alpine ELF functional oracles per system QEMU.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m137-prebuilt.sh "$port"
work="build/external/m137-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m137-batch-cases.tsv

log="$logs/m137-batch.clean.log"
if ! sh "$runner"; then
    echo "[M137 BATCH FAIL] $port signed DB/font/codegen/backup original programs" >&2
    if test -s "$log"; then
        grep -aE '^(M137_[A-Z0-9_]+_(PASS|FAIL)|\[M137 )' "$log" >&2 || true
        # Preserve early LMDB failures as well as later ISO/Zint diagnostics.
        tail -10000 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M137 BATCH FAIL] $port missing serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M137 "$port" scripts/m137-expected-markers.txt "$log" 20 2000
echo "[M137 BATCH PASS] $port 20 signed original Alpine ELF tools, real QEMU"
