#!/bin/sh
# Original signed APK functional batches M109-M140. Each profile keeps
# its own real guest script, exact markers, staging and serial evidence.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: probe-alpine-signed-batch.sh PORT PROFILE' >&2; exit 2; }
port=$1
profile=$2
case "$profile" in
    m109) count=15; fail_tail=360; marker_tail=360 ;;
    m110) count=20; fail_tail=450; marker_tail=450 ;;
    m11[1-9]|m12[0-9]|m130|m131) count=15; fail_tail=450; marker_tail=450 ;;
    m13[2-5]) count=15; fail_tail=500; marker_tail=500 ;;
    m136) count=20; fail_tail=500; marker_tail=500 ;;
    m137) count=20; fail_tail=10000; marker_tail=2000 ;;
    m138|m139) count=20; fail_tail=4000; marker_tail=2000 ;;
    m140) count=39; fail_tail=4000; marker_tail=2000 ;;
    *) exit 2 ;;
esac
. scripts/native-qemu-target.sh
sh scripts/stage-alpine-signed-batch.sh "$port" "$profile"
label=$(printf '%s' "$profile" | tr '[:lower:]' '[:upper:]')
work="build/external/$profile-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
if [ "$profile" = m110 ]; then export NAS_SYSCALL_TRACE=1; fi
export BUSYBOX_PARITY_MANIFEST="scripts/$profile-batch-cases.tsv"
log="$logs/$profile-batch.clean.log"
if ! sh "$runner"; then
    echo "[$label BATCH FAIL] $port original signed Alpine runtime" >&2
    if test -s "$log"; then
        grep -aE "^(${label}_[A-Z0-9_]+_(PASS|FAIL)|\\[$label )" "$log" >&2 || true
        tail -n "$fail_tail" "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[$label BATCH FAIL] $port missing guest serial" >&2; exit 1; }
manifest="scripts/$profile-expected-markers.txt"
sh scripts/acceptance/check-batch-markers.sh "$label" "$port" "$manifest" "$log" "$count" "$marker_tail"
echo "[$label BATCH PASS] $port $count original-program guest checks in one native QEMU boot"
