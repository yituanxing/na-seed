#!/bin/sh
# Run 24 independent original Alpine ELF semantic checks in a native QEMU guest.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-alpine-signed-batch.sh "$port" m146
work="build/external/m146-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=600
export BUSYBOX_PARITY_MANIFEST=scripts/m146-batch-cases.tsv

log="$logs/m146-batch.clean.log"
if ! sh "$runner"; then
    echo "[M146 NATIVE QEMU FAIL] $port original signed Alpine software" >&2
    if test -s "$log"; then
        grep -aE '^(M146_[A-Z0-9_]+_(PASS|FAIL)|\[M146 )' "$log" >&2 || :
        tail -2500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M146 NATIVE QEMU FAIL] missing serial $port" >&2; exit 1; }
sh scripts/acceptance/check-batch-markers.sh M146 "$port" scripts/m146-expected-markers.txt "$log" 24 0
echo "[M146 NATIVE QEMU PASS] $port 24 new signed Alpine ELFs, 24 guest semantic checks"
