#!/bin/sh
# One real QEMU boot per ISA, 15 independently attested original ELF cases.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m131-prebuilt.sh "$port"
work="build/external/m131-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m131-batch-cases.tsv

log="$logs/m131-batch.clean.log"
if ! sh "$runner"; then
    echo "[M131 BATCH FAIL] $port 15 original Netpbm ELF tools" >&2
    if test -s "$log"; then
        grep -aE '^(M131_[A-Z0-9_]+_(PASS|FAIL)|\[M131 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M131 BATCH FAIL] $port missing serial" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M131_*_PASS) ;; *) echo "[M131 BAD MARKER] $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M131 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$found" -ne 1 ]; then
        echo "[M131 BATCH FAIL] $port $marker appears $found times (need 1)" >&2
        tail -450 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M131_BATCH_PASS ]; then summary=$((summary + 1));
    else count=$((count + 1)); fi
done < scripts/m131-expected-markers.txt
[ "$count" -eq 15 ] && [ "$summary" -eq 1 ] || {
    echo "[M131 BATCH FAIL] $port expected 15 distinct original markers" >&2
    exit 1
}
echo "[M131 BATCH PASS] $port 15 signed original Alpine ELFs, real QEMU"
