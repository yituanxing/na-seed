#!/bin/sh
# Native QEMU, exact 30 NEW signed Alpine images, meaningful data per program.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m159-prebuilt.sh "$port"
work="build/external/m159-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=900
export BUSYBOX_PARITY_MANIFEST=scripts/m159-batch-cases.tsv
log="$logs/m159-batch.clean.log"
if ! sh "$runner"; then
    echo "[M159 QEMU FAIL] $port original Alpine SVN/NSS/image programs" >&2
    if test -s "$log"; then
        grep -aE '^(M159_[A-Z0-9_]+_(PASS|FAIL)|\[M159 )' "$log" >&2 || :
        tail -850 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo '[M159 FAIL] no real guest QEMU serial' >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M159_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M159 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M159 QEMU FAIL] $port $marker occurs $found times" >&2
        exit 1
    }
    if [ "$marker" = M159_BATCH_PASS ]; then summary=$((summary+1))
    else count=$((count+1)); fi
done < scripts/m159-expected-markers.txt
[ "$count" -eq 30 ] && [ "$summary" -eq 1 ] || exit 1
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M159 QEMU FAIL] $port original ELF never execve'd: /$elf" >&2
        exit 1
    }
done < scripts/m159-original-elfs.txt
echo "[M159 NATIVE QEMU PASS] $port 30 new signed Alpine ELF meaningful SVN/NSS/image workloads"
