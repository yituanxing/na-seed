#!/bin/sh
# Fifteen globally new signed Linux PDF programs in one native NaSeed QEMU;
# all PDF fixtures are built on the host, never inside the guest.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-alpine-signed-batch.sh "$port" m121
work="build/external/m121-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m121-batch-cases.tsv

log="$logs/m121-batch.clean.log"
if ! sh "$runner"; then
    echo "[M121 BATCH FAIL] $port native signed PDF functional suite" >&2
    if test -s "$log"; then
        grep -aE '^(M121_[A-Z0-9_]+_(PASS|FAIL)|\[M121 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M121 BATCH FAIL] $port no guest serial" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M121 "$port" scripts/m121-expected-markers.txt "$log" 15 450
echo "[M121 BATCH PASS] $port 15 original signed PDF ELFs, single QEMU, no guest compilation"
