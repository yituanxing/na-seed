#!/bin/sh
# One host-level real ext4 integration, pinned upstream and no userland fake.
# Large third-party source is built externally, never inside NaSeed/QEMU.
set -eu
case "${1:-}" in ''|sync-crash-only) ;; *) echo "usage: $0 [sync-crash-only]" >&2; exit 2 ;; esac
revision=58bcf89a121b72d4fb66334f1693d3b30e4cb9c5
for cmd in cmake cc mke2fs e2fsck debugfs truncate; do command -v "$cmd" >/dev/null; done
root=build/external/lwext4
# The pinned audited upstream source is part of THIS repository, not an
# opportunistic network clone on every developer machine or CI run.
vendor=third_party/lwext4
test -f "$vendor/LICENSE"
test -f "$vendor/src/ext4.c"
test -f "$vendor/include/ext4.h"
grep -Fxq "$revision" "$vendor/UPSTREAM_REVISION"
cmake -S "$vendor" -B "$root/host" -DLIB_ONLY=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$root/host" --target lwext4 -j 4 >/dev/null
cc -std=c11 -O2 -Wall -Wextra -Werror -I "$vendor/include" \
    -I "$root/host/include" -I kernel \
    scripts/test-lwext4-ramdisk.c ports/lwext4/lwext4_block.c \
    kernel/block/device.c kernel/block/ramdisk.c kernel/lib/string.c \
    "$root/host/src/liblwext4.a" -o "$root/ramdisk-test"
check_sync_crash() {
    truncate -s 32M "$root/sync-crash.img"
    mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' "$root/sync-crash.img"
    "$root/ramdisk-test" "$root/sync-crash.img" sync-crash
    "$root/ramdisk-test" "$root/sync-crash.img" sync-reopen
    # The strict oracle catches stale total free counters, not just
    # e2fsck's often-zero read-only exit status.
    sh scripts/check-ext4-fsck-clean.sh "$root/sync-crash.img"
    echo '[LWEXT4 ORDERED SYNC CRASH PASS] exact persistent file and clean superblock counters'
}
check_sparse_read() {
    # Deliberately make an inode with a nonzero logical size but ZERO
    # allocated blocks. A 4-KiB mmap/pread must not copy physical ext4
    # block zero (which contains live filesystem metadata) into user RAM.
    truncate -s 32M "$root/sparse-read.img"
    mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' "$root/sparse-read.img"
    debugfs -w -R 'write /dev/null /sparse-hole' "$root/sparse-read.img" >/dev/null 2>&1
    debugfs -w -R 'set_inode_field /sparse-hole size 32768' "$root/sparse-read.img" >/dev/null 2>&1
    test -z "$(debugfs -R 'blocks /sparse-hole' "$root/sparse-read.img" 2>/dev/null)" || {
        echo '[LWEXT4 SPARSE READ FAIL] fixture unexpectedly allocated data blocks' >&2
        exit 1
    }
    "$root/ramdisk-test" "$root/sparse-read.img" sparse-verify
    sh scripts/check-ext4-fsck-clean.sh "$root/sparse-read.img"
    echo '[LWEXT4 SPARSE HOLE ORACLE PASS] 32-KiB inode, zero physical data blocks, exact full/partial zero reads, clean fsck'
}
if [ "${1:-}" = sync-crash-only ]; then
    check_sparse_read
    check_sync_crash
    exit 0
fi
truncate -s 32M "$root/ext4.img"
mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' "$root/ext4.img"
"$root/ramdisk-test" "$root/ext4.img" write
e2fsck -fn "$root/ext4.img"
"$root/ramdisk-test" "$root/ext4.img" verify
cc -std=c11 -O2 -Wall -Wextra -Werror -I "$vendor/include" \
  -I "$root/host/include" -I kernel \
  scripts/test-lwext4-vfs.c ports/lwext4/guest_vfs.c \
  kernel/fs/vfs.c kernel/fs/lookup.c kernel/task/resources.c \
  scripts/inotify-vfs-test-stubs.c \
  ports/lwext4/lwext4_block.c \
  kernel/block/device.c kernel/block/ramdisk.c kernel/block/partition.c \
  kernel/lib/string.c kernel/mm/heap.c \
  "$root/host/src/liblwext4.a" -o "$root/vfs-test"
"$root/vfs-test" "$root/ext4.img"
"$root/vfs-test" "$root/ext4.img" write
e2fsck -fn "$root/ext4.img"
"$root/vfs-test" "$root/ext4.img" verify

e2fsck -fn "$root/ext4.img"
check_sync_crash
check_sparse_read
echo "[LWEXT4 RAMDISK PASS] real pinned upstream ext4 create/write/rename/link/unlink/remount/fsck"
