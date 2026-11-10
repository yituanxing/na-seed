#!/bin/sh
# Real native system QEMU, exact ELF inventory and per-program real data tests.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m154-prebuilt.sh "$port"
work="build/external/m154-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export BUSYBOX_PARITY_MANIFEST=scripts/m154-batch-cases.tsv
log="$logs/m154-batch.clean.log"
if ! sh "$runner"; then
    echo "[M154 NATIVE QEMU FAIL] $port authentic Alpine conversion semantics" >&2
    if test -s "$log"; then
        grep -aE '^(M154_[A-Z0-9_]+_(PASS|FAIL)|\[M154 )' "$log" >&2 || :
        tail -900 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M154 FAIL] no $port QEMU serial log" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M154_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M154 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M154 QEMU FAIL] $port $marker appears $found times" >&2; exit 1;
    }
    if [ "$marker" = M154_BATCH_PASS ]; then summary=$((summary+1))
    else count=$((count+1)); fi
done < scripts/m154-expected-markers.txt
[ "$count" -eq 30 ] && [ "$summary" -eq 1 ] || {
    echo "[M154 QEMU FAIL] $port expected 30 independent semantics" >&2; exit 1;
}
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M154 QEMU FAIL] $port original /$elf did not enter execve" >&2
        exit 1
    }
done < scripts/m154-original-elfs.txt
echo "[M154 NATIVE QEMU PASS] $port 30 new signed Alpine ELFs, 30 real conversion/data checks"
