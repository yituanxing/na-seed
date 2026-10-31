#!/bin/sh
# Exact batch of 20 new native Alpine ELF functional oracles per system QEMU.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m136-prebuilt.sh "$port"
work="build/external/m136-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m136-batch-cases.tsv

log="$logs/m136-batch.clean.log"
if ! sh "$runner"; then
    echo "[M136 BATCH FAIL] $port signed parser/terminal/Unix original programs" >&2
    if test -s "$log"; then
        grep -aE '^(M136_[A-Z0-9_]+_(PASS|FAIL)|\[M136 )' "$log" >&2 || true
        tail -500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M136 BATCH FAIL] $port missing serial" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M136_*_PASS) ;; *) echo "[M136 BAD MARKER] $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M136 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$found" -ne 1 ]; then
        echo "[M136 BATCH FAIL] $port $marker appears $found times (expected once)" >&2
        tail -500 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M136_BATCH_PASS ]; then summary=$((summary + 1));
    else count=$((count + 1)); fi
done < scripts/m136-expected-markers.txt
[ "$count" -eq 20 ] && [ "$summary" -eq 1 ] || {
    echo "[M136 BATCH FAIL] $port expected 20 independently tested ELFs" >&2
    exit 1
}
echo "[M136 BATCH PASS] $port 20 signed original Alpine ELF tools, real QEMU"
