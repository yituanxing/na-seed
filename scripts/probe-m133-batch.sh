#!/bin/sh
# Exact batch of 15 new native Alpine ELF functional oracles per system QEMU.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m133-prebuilt.sh "$port"
work="build/external/m133-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m133-batch-cases.tsv

log="$logs/m133-batch.clean.log"
if ! sh "$runner"; then
    echo "[M133 BATCH FAIL] $port all 15 signed graph/xz/text/FAT programs" >&2
    if test -s "$log"; then
        grep -aE '^(M133_[A-Z0-9_]+_(PASS|FAIL)|\[M133 )' "$log" >&2 || true
        tail -500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M133 BATCH FAIL] $port missing serial" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M133_*_PASS) ;; *) echo "[M133 BAD MARKER] $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M133 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$found" -ne 1 ]; then
        echo "[M133 BATCH FAIL] $port $marker appears $found times (expected once)" >&2
        tail -500 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M133_BATCH_PASS ]; then summary=$((summary + 1));
    else count=$((count + 1)); fi
done < scripts/m133-expected-markers.txt
[ "$count" -eq 15 ] && [ "$summary" -eq 1 ] || {
    echo "[M133 BATCH FAIL] $port expected 15 independently tested ELFs" >&2
    exit 1
}
echo "[M133 BATCH PASS] $port 15 signed original Alpine ELF tools, real QEMU"
