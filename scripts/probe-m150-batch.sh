#!/bin/sh
# Native QEMU 26-case semantic admission after host signed-APK staging.
# This is a real guest, never a host replay of Alpine command output.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m150-prebuilt.sh "$port"
work="build/external/m150-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=650
export BUSYBOX_PARITY_MANIFEST=scripts/m150-batch-cases.tsv
log="$logs/m150-batch.clean.log"
if ! sh "$runner"; then
    echo "[M150 NATIVE QEMU FAIL] $port genuine signed Alpine ELFs" >&2
    if test -s "$log"; then
        grep -aE '^(M150_[A-Z0-9_]+_(PASS|FAIL)|\[M150 )' "$log" >&2 || :
        tail -2500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || {
    echo "[M150 NATIVE QEMU FAIL] missing serial $port" >&2; exit 1;
}
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M150_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*)
        echo "[M150 DUPLICATE MARKER] $marker" >&2; exit 2 ;;
    esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M150 NATIVE QEMU FAIL] $port $marker appears $found times" >&2
        exit 1
    }
    if [ "$marker" = M150_BATCH_PASS ]; then summary=$((summary+1));
    else count=$((count+1)); fi
done < scripts/m150-expected-markers.txt
[ "$count" -eq 26 ] && [ "$summary" -eq 1 ] || {
    echo "[M150 NATIVE QEMU FAIL] $port expected 26 semantics + summary" >&2
    exit 1
}
echo "[M150 NATIVE QEMU PASS] $port 26 new signed Alpine ELFs, 26 guest function tests"
