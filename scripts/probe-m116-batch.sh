#!/bin/sh
# Run all fifteen original Alpine M116 applications inside one NaSeed QEMU.
# The Linux host installs the signed APKs, NaSeed only runs real programs.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: probe-m116-batch.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m116-prebuilt.sh "$port"
work="build/external/m116-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_MANIFEST=scripts/m116-batch-cases.tsv

log="$logs/m116-batch.clean.log"
if ! sh "$runner"; then
    echo "[M116 BATCH FAIL] $port original signed Alpine Linux ELF runtime" >&2
    if test -s "$log"; then
        grep -aE '^(M116_[A-Z0-9_]+_(PASS|FAIL)|\[M116 )' "$log" >&2 || true
        tail -450 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || {
    echo "[M116 BATCH FAIL] $port missing authentic guest QEMU serial" >&2
    exit 1
}
manifest=scripts/m116-expected-markers.txt
test -s "$manifest"
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M116_*_PASS) ;; *) echo "[M116 BATCH FAIL] invalid marker $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M116 BATCH FAIL] duplicate marker $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    count_found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$count_found" -ne 1 ]; then
        echo "[M116 BATCH FAIL] $port $marker emitted $count_found guest attestations" >&2
        tail -450 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M116_BATCH_PASS ]; then summary=$((summary + 1)); else count=$((count + 1)); fi
done < "$manifest"
[ "$count" -eq 15 ] && [ "$summary" -eq 1 ] || {
    echo "[M116 BATCH FAIL] expected 15 original executable markers plus summary, got $count/$summary" >&2
    exit 1
}
echo "[M116 BATCH PASS] $port 15 signed original Linux applications, one QEMU boot, host-prebuilt"
