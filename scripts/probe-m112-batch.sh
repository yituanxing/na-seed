#!/bin/sh
# One genuine NaSeed QEMU boot per native ISA runs all original M112 Linux ELFs.
# Only the Linux CI host downloads/installs official signed APKs.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m112-batch.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m112-prebuilt.sh "$port"
work="build/external/m112-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m112-batch-cases.tsv

log="$logs/m112-batch.clean.log"
if ! sh "$runner"; then
    echo "[M112 BATCH FAIL] $port real signed Alpine Linux runtime" >&2
    if test -s "$log"; then
        grep -aE '^(M112_[A-Z0-9_]+_(PASS|FAIL)|\[M112 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M112 BATCH FAIL] $port missing guest serial" >&2; exit 1; }

manifest=scripts/m112-expected-markers.txt
test -s "$manifest"
sh scripts/acceptance/check-batch-markers.sh M112 "$port" "$manifest" "$log" 15 450
echo "[M112 BATCH PASS] $port 15 signed original Linux applications, one QEMU boot, host-prebuilt"
