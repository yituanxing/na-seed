#!/bin/sh
# Real QEMU execution of 33 previously unadmitted Alpine image programs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m163-prebuilt.sh "$port"
work="build/external/m163-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=900
export BUSYBOX_PARITY_MANIFEST=scripts/m163-batch-cases.tsv
log="$logs/m163-batch.clean.log"
if ! sh "$runner"; then
    echo "[M163 NATIVE FAIL] $port 33 genuine signed image programs" >&2
    if test -s "$log"; then
        grep -aE '^(M163_[A-Z0-9_]+_(PASS|FAIL)|\[M163 )' "$log" >&2 || :
        tail -700 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || exit 1
count=0
summary=0
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M163 MARKER FAIL] $port $marker: found $found" >&2
        exit 1
    }
    case "$marker" in M163_BATCH_PASS) summary=$((summary+1)) ;; *) count=$((count+1)) ;; esac
done < scripts/m163-expected-markers.txt
[ "$count" -eq 33 ] && [ "$summary" -eq 1 ] || exit 1
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M163 EXEC FAIL] $port real ELF never executed: /$elf" >&2
        exit 1
    }
done < scripts/m163-original-elfs.txt
echo "[M163 THREE ISA PASS] $port 33 new signed Alpine ELFs: pixels, transforms, PICT/ICO, terminal"
