#!/bin/sh
# One real NaSeed QEMU boot per architecture for all 20 signed original apps.
# Host stages APKs and target rootfs; the guest ONLY runs prebuilt executables.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m110-batch.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m110-prebuilt.sh "$port"
work="build/external/m110-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export NAS_SYSCALL_TRACE=1
export BUSYBOX_PARITY_MANIFEST=scripts/m110-batch-cases.tsv

log="$logs/m110-batch.clean.log"
if ! sh "$runner"; then
    echo "[M110 BATCH FAIL] $port actual original signed Alpine runtime" >&2
    if test -s "$log"; then
        grep -aE '^(M110_[A-Z0-9_]+_(PASS|FAIL)|\[M110 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M110 BATCH FAIL] $port missing original guest serial" >&2; exit 1; }

# An aggregate PASS cannot mask a missing or skipped real executable case.
manifest=scripts/m110-expected-markers.txt
test -s "$manifest"
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M110_*_PASS) ;; *)
        echo "[M110 BATCH FAIL] invalid marker: $marker" >&2
        exit 2 ;;
    esac
    case "$seen" in *" $marker "*)
        echo "[M110 BATCH FAIL] duplicate expected marker: $marker" >&2
        exit 2 ;;
    esac
    seen="$seen$marker "
    count_found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$count_found" -ne 1 ]; then
        echo "[M110 BATCH FAIL] $port $marker had $count_found actual attestations" >&2
        tail -450 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M110_BATCH_PASS ]; then
        summary=$((summary + 1))
    else
        count=$((count + 1))
    fi
done < "$manifest"
[ "$count" -eq 20 ] && [ "$summary" -eq 1 ] || {
    echo "[M110 BATCH FAIL] $port expected exactly 20 distinct program checks and one batch summary; got $count/$summary" >&2
    exit 1
}
echo "[M110 BATCH PASS] $port $count new genuine-program functional tests, one QEMU boot, host-staged signed ELFs"
