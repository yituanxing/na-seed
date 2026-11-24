#!/bin/sh
# M138: run all 20 original LLVM programs in one native system-QEMU boot.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m138-prebuilt.sh "$port"
work="build/external/m138-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m138-batch-cases.tsv

log="$logs/m138-batch.clean.log"
if ! sh "$runner"; then
    echo "[M138 BATCH FAIL] $port signed original LLVM 21 programs" >&2
    if test -s "$log"; then
        grep -aE '^(M138_[A-Z0-9_]+_(PASS|FAIL)|\[M138 )' "$log" >&2 || true
        tail -4000 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M138 BATCH FAIL] $port missing serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M138 "$port" scripts/m138-expected-markers.txt "$log" 20 2000
echo "[M138 BATCH PASS] $port 20 original signed LLVM 21 ELFs, native QEMU"
