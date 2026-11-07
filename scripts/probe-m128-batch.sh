#!/bin/sh
# One native NaSeed QEMU guest per ISA: 15 original filesystem ELF tools.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m128-prebuilt.sh "$port"
work="build/external/m128-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m128-batch-cases.tsv

log="$logs/m128-batch.clean.log"
if ! sh "$runner"; then
    echo "[M128 BATCH FAIL] $port 15 signed original filesystem programs" >&2
    if test -s "$log"; then
        grep -aE '^(M128_[A-Z0-9_]+_(PASS|FAIL)|\[M128 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M128 BATCH FAIL] $port no guest serial" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M128_*_PASS) ;; *) echo "[M128 BATCH FAIL] invalid marker $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M128 BATCH FAIL] duplicate marker $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$found" -ne 1 ]; then
        echo "[M128 BATCH FAIL] $port $marker attestations $found (expected exactly 1)" >&2
        tail -450 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M128_BATCH_PASS ]; then summary=$((summary + 1)); else count=$((count + 1)); fi
done < scripts/m128-expected-markers.txt
[ "$count" -eq 15 ] && [ "$summary" -eq 1 ] || {
    echo "[M128 BATCH FAIL] need 15+summary, got $count/$summary" >&2
    exit 1
}
echo "[M128 BATCH PASS] $port 15 original signed filesystem ELF, single QEMU"
