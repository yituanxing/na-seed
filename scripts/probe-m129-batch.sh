#!/bin/sh
# Whole 15-program suite in a single real system-QEMU NaSeed guest per ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m129-prebuilt.sh "$port"
work="build/external/m129-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m129-batch-cases.tsv

log="$logs/m129-batch.clean.log"
if ! sh "$runner"; then
    echo "[M129 BATCH FAIL] $port 15 original process/monitoring ELFs" >&2
    if test -s "$log"; then
        grep -aE '^(M129_[A-Z0-9_]+_(PASS|FAIL)|\[M129 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M129 BATCH FAIL] $port no serial" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M129_*_PASS) ;; *) echo "[M129 BATCH FAIL] invalid marker $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M129 BATCH FAIL] duplicate marker $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$found" -ne 1 ]; then
        echo "[M129 BATCH FAIL] $port $marker attestations $found, expected 1" >&2
        tail -450 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M129_BATCH_PASS ]; then summary=$((summary + 1)); else count=$((count + 1)); fi
done < scripts/m129-expected-markers.txt
[ "$count" -eq 15 ] && [ "$summary" -eq 1 ] || {
    echo "[M129 BATCH FAIL] need 15+summary, got $count/$summary" >&2
    exit 1
}
echo "[M129 BATCH PASS] $port 15 original signed ELF, single QEMU"
