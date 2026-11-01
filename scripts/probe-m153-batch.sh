#!/bin/sh
# Genuine native QEMU serial admission: all 36 original upstream ELFs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m153-prebuilt.sh "$port"
work="build/external/m153-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export BUSYBOX_PARITY_MANIFEST=scripts/m153-batch-cases.tsv
log="$logs/m153-batch.clean.log"
if ! sh "$runner"; then
    echo "[M153 NATIVE QEMU FAIL] $port genuine signed Alpine ELFs" >&2
    if test -s "$log"; then
        grep -aE '^(M153_[A-Z0-9_]+_(PASS|FAIL)|\[M153 )' "$log" >&2 || :
        tail -2500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M153 NATIVE QEMU FAIL] missing serial $port" >&2; exit 1; }
count=0 summary=0 seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M153_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M153 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M153 NATIVE QEMU FAIL] $port $marker appears $found times" >&2; exit 1; }
    if [ "$marker" = M153_BATCH_PASS ]; then summary=$((summary+1));
    else count=$((count+1)); fi
done < scripts/m153-expected-markers.txt
[ "$count" -eq 36 ] && [ "$summary" -eq 1 ] || {
    echo "[M153 NATIVE QEMU FAIL] $port expected 36 semantic markers + summary" >&2; exit 1; }
echo "[M153 NATIVE QEMU PASS] $port 36 original signed ELF programs, 36 real guest functional checks"
