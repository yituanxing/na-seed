#!/bin/sh
# Run 24 independent original Alpine ELF semantic checks in a native QEMU guest.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m146-prebuilt.sh "$port"
work="build/external/m146-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=600
export BUSYBOX_PARITY_MANIFEST=scripts/m146-batch-cases.tsv

log="$logs/m146-batch.clean.log"
if ! sh "$runner"; then
    echo "[M146 NATIVE QEMU FAIL] $port original signed Alpine software" >&2
    if test -s "$log"; then
        grep -aE '^(M146_[A-Z0-9_]+_(PASS|FAIL)|\[M146 )' "$log" >&2 || :
        tail -2500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M146 NATIVE QEMU FAIL] missing serial $port" >&2; exit 1; }
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M146_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M146 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M146 NATIVE QEMU FAIL] $port $marker appears $found times" >&2
        exit 1
    }
    if [ "$marker" = M146_BATCH_PASS ]; then summary=$((summary+1));
    else count=$((count+1)); fi
done < scripts/m146-expected-markers.txt
[ "$count" -eq 24 ] && [ "$summary" -eq 1 ] || {
    echo "[M146 NATIVE QEMU FAIL] $port expected 24 independent semantics" >&2
    exit 1
}
echo "[M146 NATIVE QEMU PASS] $port 24 new signed Alpine ELFs, 24 guest semantic checks"
