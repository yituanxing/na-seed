#!/bin/sh
# Native QEMU end-to-end only: 35 new signed Alpine ELFs, 22 format roundtrips.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m148-prebuilt.sh "$port"
work="build/external/m148-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=660
export BUSYBOX_PARITY_MANIFEST=scripts/m148-batch-cases.tsv

log="$logs/m148-batch.clean.log"
if ! sh "$runner"; then
    echo "[M148 NATIVE QEMU FAIL] $port original codec roundtrips" >&2
    if test -s "$log"; then
        grep -aE '^(M148_[A-Z0-9_]+_(PASS|FAIL)|\[M148 )' "$log" >&2 || :
        tail -2500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M148 NATIVE QEMU FAIL] missing $port serial" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M148_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M148 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M148 NATIVE QEMU FAIL] $port $marker appears $found times" >&2
        exit 1
    }
    if [ "$marker" = M148_BATCH_PASS ]; then summary=$((summary+1));
    else count=$((count+1)); fi
done < scripts/m148-expected-markers.txt
[ "$count" -eq 35 ] && [ "$summary" -eq 1 ] || {
    echo "[M148 NATIVE QEMU FAIL] $port 35 exact ELF markers required" >&2
    exit 1
}
# A marker from the parent BusyBox shell does not by itself prove every guest
# ELF ran: require NaSeed's real execve replacement trace for all 35 paths.
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M148 NATIVE QEMU FAIL] $port missing authentic guest execve /$elf" >&2
        exit 1
    }
done < scripts/m148-original-elfs.txt
echo "[M148 NATIVE QEMU PASS] $port 35 new original Alpine codecs through image roundtrips"
