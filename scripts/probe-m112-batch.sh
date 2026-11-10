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
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M112_*_PASS) ;; *) echo "[M112 BATCH FAIL] invalid marker $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M112 BATCH FAIL] duplicate marker $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    count_found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$count_found" -ne 1 ]; then
        echo "[M112 BATCH FAIL] $port $marker emitted $count_found genuine guest attestations" >&2
        tail -450 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M112_BATCH_PASS ]; then summary=$((summary + 1)); else count=$((count + 1)); fi
done < "$manifest"
[ "$count" -eq 15 ] && [ "$summary" -eq 1 ] || {
    echo "[M112 BATCH FAIL] expected 15 distinct programs plus one summary, got $count/$summary" >&2
    exit 1
}
echo "[M112 BATCH PASS] $port 15 signed original Linux applications, one QEMU boot, host-prebuilt"
