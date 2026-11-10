#!/bin/sh
# Run the twenty M139 original executables in one native system-QEMU boot.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m139-prebuilt.sh "$port"
work="build/external/m139-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m139-batch-cases.tsv

log="$logs/m139-batch.clean.log"
if ! sh "$runner"; then
    echo "[M139 BATCH FAIL] $port signed font/image/process/crypto programs" >&2
    if test -s "$log"; then
        grep -aE '^(M139_[A-Z0-9_]+_(PASS|FAIL)|\[M139 )' "$log" >&2 || true
        tail -4000 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M139 BATCH FAIL] $port missing serial" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M139_*_PASS) ;; *) echo "[M139 BAD MARKER] $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M139 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$found" -ne 1 ]; then
        echo "[M139 BATCH FAIL] $port $marker appears $found times (expected once)" >&2
        tail -2000 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M139_BATCH_PASS ]; then summary=$((summary + 1));
    else count=$((count + 1)); fi
done < scripts/m139-expected-markers.txt
[ "$count" -eq 20 ] && [ "$summary" -eq 1 ] || {
    echo "[M139 BATCH FAIL] $port expected 20 independent ELF functional tests" >&2
    exit 1
}
echo "[M139 BATCH PASS] $port 20 original signed Alpine ELFs, native QEMU"
