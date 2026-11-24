#!/bin/sh
# Batch 2: ONE genuine QEMU boot per ISA, 15 original signed Linux programs.
# Reuse the M80 canonical APK and native-QEMU owners, not a private installer
# or one expensive kernel rebuild per application. Guest tests own oracles.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m109-batch.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-alpine-signed-batch.sh "$port" m109
work="build/external/m109-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m109-batch-cases.tsv

log="$logs/m109-batch.clean.log"
if ! sh "$runner"; then
    echo "[M109 BATCH FAIL] $port original signed Alpine runtime" >&2
    if test -s "$log"; then
        # Always surface the WHOLE fifteen-program scoreboard even when a
        # kernel trace pushes early failures beyond the serial-log tail.
        grep -aE '^(M109_[A-Z0-9_]+_(PASS|FAIL)|\[M109 )' "$log" >&2 || true
        tail -360 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M109 BATCH FAIL] $port missing guest serial" >&2; exit 1; }

# A single summary string never proves that a whole application batch ran.
# Require every unique, independently asserted original-program marker.
manifest=scripts/m109-expected-markers.txt
test -s "$manifest"
sh scripts/acceptance/check-batch-markers.sh M109 "$port" "$manifest" "$log" 15 360
count=15
echo "[M109 BATCH PASS] $port $count original-program guest checks in one QEMU boot, host-installed signed ELF only"
