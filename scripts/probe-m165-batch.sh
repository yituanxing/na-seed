#!/bin/sh
# Native 3-ISA execution of thirty new signed Alpine image applications.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m165-prebuilt.sh "$port"
work="build/external/m165-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=900
export BUSYBOX_PARITY_MANIFEST=scripts/m165-batch-cases.tsv
log="$logs/m165-batch.clean.log"
if ! sh "$runner"; then
    echo "[M165 NATIVE FAIL] $port thirty genuine image programs" >&2
    if test -s "$log"; then
        grep -aE '^(M165_[A-Z0-9_]+_(PASS|FAIL)|\[M165 )' "$log" >&2 || :
        tail -700 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || exit 1
count=0
summary=0
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M165 MARKER FAIL] $port $marker" >&2; exit 1;
    }
    case "$marker" in M165_BATCH_PASS) summary=$((summary+1)) ;; *) count=$((count+1)) ;; esac
done < scripts/m165-expected-markers.txt
[ "$count" -eq 30 ] && [ "$summary" -eq 1 ] || exit 1
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M165 EXEC FAIL] $port original ELF not run: /$elf" >&2; exit 1;
    }
done < scripts/m165-original-elfs.txt
echo "[M165 NATIVE PASS] $port 30 new signed Alpine image and actual codec operations"
