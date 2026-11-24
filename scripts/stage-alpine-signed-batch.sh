#!/bin/sh
# One signed APK staging owner for the original M109–M118 batches.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: stage-alpine-signed-batch.sh PORT PROFILE' >&2; exit 2; }
port=$1
profile=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$profile" in
    m109|m111|m112|m113|m114|m115) count=15; mode=count ;;
    m110) count=21; mode=count ;;
    m116|m117|m118) count=15; mode=unique ;;
    *) exit 2 ;;
esac
label=$(printf '%s' "$profile" | tr '[:lower:]' '[:upper:]')
manifest="scripts/$profile-original-elfs.txt"
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" "$profile-batch"
work="build/external/$profile-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
require_fixtures() {
    for fixture in "$@"; do test -s "$root/tests/$fixture" || exit 1; done
}
case "$profile" in
    m118) require_fixtures m118-debug.o m118-debug-other.o m118-debug.elf m118-textrel.so ;;
esac
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/$profile-original-signed-elfs.sha256" "$count" "$mode" "$label"
echo "[$label HOST STAGE PASS] $port $count original signed Alpine ELFs ($mode); native guest acceptance remains separate"
