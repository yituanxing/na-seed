#!/bin/sh
# Require native QEMU guest execution of thirty independently admitted real
# original Alpine image programs, each on a reversible image format case.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m158-prebuilt.sh "$port"
work="build/external/m158-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export BUSYBOX_PARITY_MANIFEST=scripts/m158-batch-cases.tsv
log="$logs/m158-batch.clean.log"
if ! sh "$runner"; then
    echo "[M158 NATIVE QEMU FAIL] $port 30 original Alpine reversible image codecs" >&2
    if test -s "$log"; then
        grep -aE '^(M158_[A-Z0-9_]+_(PASS|FAIL)|\[M158 )' "$log" >&2 || :
        tail -900 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M158 FAIL] no real guest QEMU log" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M158_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*)
        echo "[M158 DUPLICATE MARKER] $marker" >&2; exit 2 ;;
    esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M158 QEMU FAIL] $port $marker appeared $found times" >&2
        exit 1
    }
    if [ "$marker" = M158_BATCH_PASS ]; then summary=$((summary+1))
    else count=$((count+1)); fi
done < scripts/m158-expected-markers.txt
[ "$count" -eq 30 ] && [ "$summary" -eq 1 ] || {
    echo "[M158 QEMU FAIL] $port require 30 semantic cases, got $count" >&2
    exit 1
}
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M158 QEMU FAIL] $port ELF not execve'd: /$elf" >&2
        exit 1
    }
done < scripts/m158-original-elfs.txt
echo "[M158 NATIVE QEMU PASS] $port 30 original signed Alpine image codecs"
