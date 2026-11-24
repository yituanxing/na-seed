#!/bin/sh
# Run the twenty M139 original executables in one native system-QEMU boot.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m139-prebuilt.sh "$port"
work="build/external/m139-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m139-batch-cases.tsv

log="$logs/m139-batch.clean.log"
if ! sh "$runner"; then
    echo "[M139 BATCH FAIL] $port signed font/image/process/crypto programs" >&2
    if test -s "$log"; then
        grep -aE '^(M139_[A-Z0-9_]+_(PASS|FAIL)|\[M139 )' "$log" >&2 || true
        tail -4000 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M139 BATCH FAIL] $port missing serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M139 "$port" scripts/m139-expected-markers.txt "$log" 20 2000
echo "[M139 BATCH PASS] $port 20 original signed Alpine ELFs, native QEMU"
