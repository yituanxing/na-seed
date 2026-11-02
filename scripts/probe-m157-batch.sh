#!/bin/sh
# Native QEMU runs 30 NEW original Alpine ELF image programs. Program success
# requires a real fixture transform, independent expected marker and execve.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m157-prebuilt.sh "$port"
work="build/external/m157-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export BUSYBOX_PARITY_MANIFEST=scripts/m157-batch-cases.tsv
log="$logs/m157-batch.clean.log"
if ! sh "$runner"; then
    echo "[M157 NATIVE QEMU FAIL] $port original Alpine image operations" >&2
    if test -s "$log"; then
        grep -aE '^(M157_[A-Z0-9_]+_(PASS|FAIL)|\[M157 )' "$log" >&2 || :
        tail -900 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M157 FAIL] no real guest QEMU log" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M157_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M157 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M157 QEMU FAIL] $port $marker appeared $found times" >&2
        exit 1
    }
    if [ "$marker" = M157_BATCH_PASS ]; then summary=$((summary+1))
    else count=$((count+1)); fi
done < scripts/m157-expected-markers.txt
[ "$count" -eq 30 ] && [ "$summary" -eq 1 ] || {
    echo "[M157 QEMU FAIL] $port require 30 semantic cases, got $count" >&2
    exit 1
}
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M157 QEMU FAIL] $port ELF not execve'd: /$elf" >&2
        exit 1
    }
done < scripts/m157-original-elfs.txt
echo "[M157 NATIVE QEMU PASS] $port 30 distinct signed Alpine ELF image semantics"
