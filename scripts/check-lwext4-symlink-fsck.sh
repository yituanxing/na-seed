#!/bin/sh
# Real pinned lwext4 fast/long symlink boundary and physical ext4 fsck oracle.
# Build products and disk image are isolated in a temporary directory; leave
# the image intact on a failure so the exact inode can be investigated.
set -eu
for cmd in cmake cc truncate mke2fs e2fsck; do
    command -v "$cmd" >/dev/null || {
        echo "[SYMLINK FSCK BLOCKED] missing required host command: $cmd" >&2
        exit 127
    }
done
revision=58bcf89a121b72d4fb66334f1693d3b30e4cb9c5
vendor=third_party/lwext4
test -f "$vendor/LICENSE"
test -f "$vendor/src/ext4.c"
grep -Fxq "$revision" "$vendor/UPSTREAM_REVISION"

scratch=$(mktemp -d "${TMPDIR:-/tmp}/naseed-lwext4-symlink-XXXXXX")
finish() {
    status=$?
    trap - EXIT
    if [ "$status" -eq 0 ]; then
        rm -rf -- "$scratch"
    else
        echo "[SYMLINK FSCK EVIDENCE] preserved failing image/build: $scratch" >&2
    fi
    exit "$status"
}
trap finish EXIT
cmake -S "$vendor" -B "$scratch/host" -DLIB_ONLY=ON \
    -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$scratch/host" --target lwext4 -j 4 >/dev/null

cc -std=c11 -O2 -Wall -Wextra -Werror \
    -I "$vendor/include" -I "$scratch/host/include" -I kernel \
    scripts/test-lwext4-symlink-fsck.c ports/lwext4/lwext4_block.c \
    kernel/block/device.c kernel/block/ramdisk.c kernel/lib/string.c \
    "$scratch/host/src/liblwext4.a" -o "$scratch/symlink-test"

image="$scratch/symlinks.img"
truncate -s 32M "$image"
mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' "$image"
"$scratch/symlink-test" "$image" create
# Strict metadata/content oracle, beyond e2fsck's sometimes-zero exit status.
sh scripts/check-ext4-fsck-clean.sh "$image"
"$scratch/symlink-test" "$image" reopen
sh scripts/check-ext4-fsck-clean.sh "$image"
echo '[LWEXT4 SYMLINK FSCK PASS] 54/59/60/61/62/67/69/71/4095 bytes, nonzero block reuse, recreation, independent remount, strict physical e2fsck'
