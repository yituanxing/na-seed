#!/bin/sh
# One real NaSeed QEMU boot per architecture for all 20 signed original apps.
# Host stages APKs and target rootfs; the guest ONLY runs prebuilt executables.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m110-batch.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-alpine-signed-batch.sh "$port" m110
work="build/external/m110-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export NAS_SYSCALL_TRACE=1
export BUSYBOX_PARITY_MANIFEST=scripts/m110-batch-cases.tsv

log="$logs/m110-batch.clean.log"
if ! sh "$runner"; then
    echo "[M110 BATCH FAIL] $port actual original signed Alpine runtime" >&2
    if test -s "$log"; then
        grep -aE '^(M110_[A-Z0-9_]+_(PASS|FAIL)|\[M110 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M110 BATCH FAIL] $port missing original guest serial" >&2; exit 1; }

# An aggregate PASS cannot mask a missing or skipped real executable case.
manifest=scripts/m110-expected-markers.txt
test -s "$manifest"
sh scripts/acceptance/check-batch-markers.sh M110 "$port" "$manifest" "$log" 20 450
count=20
echo "[M110 BATCH PASS] $port $count new genuine-program functional tests, one QEMU boot, host-staged signed ELFs"
