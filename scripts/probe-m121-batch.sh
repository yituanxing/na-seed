#!/bin/sh
# Fifteen globally new signed Linux PDF programs in one native NaSeed QEMU;
# all PDF fixtures are built on the host, never inside the guest.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m121-prebuilt.sh "$port"
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
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M121_*_PASS) ;; *) echo "[M121 BATCH FAIL] invalid marker $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M121 BATCH FAIL] duplicate marker $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$found" -ne 1 ]; then
        echo "[M121 BATCH FAIL] $port $marker actual attestations $found (expected 1)" >&2
        tail -450 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M121_BATCH_PASS ]; then summary=$((summary + 1)); else count=$((count + 1)); fi
done < scripts/m121-expected-markers.txt
[ "$count" -eq 15 ] && [ "$summary" -eq 1 ] || {
    echo "[M121 BATCH FAIL] need 15 native app markers + summary, got $count/$summary" >&2
    exit 1
}
echo "[M121 BATCH PASS] $port 15 original signed PDF ELFs, single QEMU, no guest compilation"
