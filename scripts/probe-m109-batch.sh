#!/bin/sh
# Batch 2: ONE genuine QEMU boot per ISA, 15 original signed Linux programs.
# Reuse the M80 canonical APK and native-QEMU owners, not a private installer
# or one expensive kernel rebuild per application. Guest tests own oracles.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m109-batch.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m109-prebuilt.sh "$port"
work="build/external/m109-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export NAS_SYSCALL_TRACE=0
export BUSYBOX_PARITY_MANIFEST=scripts/m109-batch-cases.tsv

log="$logs/m109-batch.clean.log"
if ! sh "$runner"; then
    echo "[M109 BATCH FAIL] $port original signed Alpine runtime" >&2
    test ! -s "$log" || tail -360 "$log" >&2
    exit 1
fi
test -s "$log" || { echo "[M109 BATCH FAIL] $port missing guest serial" >&2; exit 1; }

# A single summary string never proves that a whole application batch ran.
# Require every unique, independently asserted original-program marker.
manifest=scripts/m109-expected-markers.txt
test -s "$manifest"
count=0
seen=' '
summary=0
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M109_*_PASS) ;; *)
        echo "[M109 BATCH FAIL] unsafe expected marker: $marker" >&2; exit 2 ;;
    esac
    case "$seen" in *" $marker "*)
        echo "[M109 BATCH FAIL] duplicate expected marker: $marker" >&2; exit 2 ;;
    esac
    seen="$seen$marker "
    count_found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$count_found" -ne 1 ]; then
        echo "[M109 BATCH FAIL] $port $marker had $count_found genuine guest attestations" >&2
        tail -360 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M109_BATCH_PASS ]; then
        summary=$((summary + 1))
    else
        count=$((count + 1))
    fi
done < "$manifest"
[ "$count" -eq 15 ] && [ "$summary" -eq 1 ] || {
    echo "[M109 BATCH FAIL] $port expected exactly 15 original software checks and one summary; got $count/$summary" >&2
    exit 1
}
echo "[M109 BATCH PASS] $port $count original-program guest checks in one QEMU boot, host-installed signed ELF only"
