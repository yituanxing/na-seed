#!/bin/sh
# No guest build: stage host-signed APK once, run all 15 image CLIs per QEMU.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m122-prebuilt.sh "$port"
work="build/external/m122-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m122-batch-cases.tsv

log="$logs/m122-batch.clean.log"
if ! sh "$runner"; then
    echo "[M122 BATCH FAIL] $port real signed original Netpbm applications" >&2
    if test -s "$log"; then
        grep -aE '^(M122_[A-Z0-9_]+_(PASS|FAIL)|\[M122 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M122 BATCH FAIL] $port no guest serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M122 "$port" scripts/m122-expected-markers.txt "$log" 15 450
echo "[M122 BATCH PASS] $port 15 real original Netpbm ELF image applications, one QEMU"
