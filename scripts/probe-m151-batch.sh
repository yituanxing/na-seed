#!/bin/sh
# True system-QEMU execution of all 35 signed native Alpine ELF code paths.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m151-prebuilt.sh "$port"
work="build/external/m151-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1536
export NAS_APP_BATCH_QEMU_TIMEOUT=900
export BUSYBOX_PARITY_MANIFEST=scripts/m151-batch-cases.tsv
log="$logs/m151-batch.clean.log"
if ! sh "$runner"; then
    echo "[M151 NATIVE QEMU FAIL] $port original media codecs" >&2
    if test -s "$log"; then
        grep -aE '^(M151_[A-Z0-9_]+_(PASS|FAIL)|\[M151 )' "$log" >&2 || :
        tail -3000 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M151 NATIVE QEMU FAIL] missing serial" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M151_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M151 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M151 NATIVE QEMU FAIL] $port $marker appears $found times" >&2
        exit 1
    }
    if [ "$marker" = M151_BATCH_PASS ]; then summary=$((summary+1));
    else count=$((count+1)); fi
done < scripts/m151-expected-markers.txt
[ "$count" -eq 35 ] && [ "$summary" -eq 1 ] || {
    echo "[M151 NATIVE QEMU FAIL] $count/35 original semantics" >&2; exit 1; }
# Markers alone cannot establish 23 separate real codecs. The test emits
# exactly one operation record after each genuine encode/decode roundtrip.
roundtrips=$(grep -Fc '[M151 ROUNDTRIP]' "$log" || :)
[ "$roundtrips" -eq 23 ] || {
    echo "[M151 NATIVE QEMU FAIL] $port only $roundtrips/23 real roundtrips" >&2
    exit 1
}
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M151 NATIVE QEMU FAIL] $port missing genuine exec /$elf" >&2
        exit 1
    }
done < scripts/m151-original-elfs.txt
echo "[M151 NATIVE QEMU PASS] $port 35 new original multimedia ELF, 23 actual media format roundtrips"
