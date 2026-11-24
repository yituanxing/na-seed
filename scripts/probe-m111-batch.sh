#!/bin/sh
# Every actual acceptance case is in one real QEMU boot per native ISA.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m111-batch.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m111-prebuilt.sh "$port"
work="build/external/m111-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m111-batch-cases.tsv

log="$logs/m111-batch.clean.log"
if ! sh "$runner"; then
    echo "[M111 BATCH FAIL] $port real signed Alpine Linux runtime" >&2
    if test -s "$log"; then
        grep -aE '^(M111_[A-Z0-9_]+_(PASS|FAIL)|\[M111 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M111 BATCH FAIL] $port missing guest serial" >&2; exit 1; }

manifest=scripts/m111-expected-markers.txt
test -s "$manifest"
sh scripts/acceptance/check-batch-markers.sh M111 "$port" "$manifest" "$log" 15 450
echo "[M111 BATCH PASS] $port 15 signed original Linux applications, one QEMU boot, host-prebuilt"
