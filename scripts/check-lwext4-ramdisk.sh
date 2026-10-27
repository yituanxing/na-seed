#!/bin/sh
# One host-level real ext4 integration, pinned upstream and no userland fake.
# Large third-party source is built externally, never inside NaSeed/QEMU.
set -eu
revision=58bcf89a121b72d4fb66334f1693d3b30e4cb9c5
for cmd in cmake cc mke2fs e2fsck truncate; do command -v "$cmd" >/dev/null; done
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
truncate -s 32M "$root/ext4.img"
mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' "$root/ext4.img"
"$root/ramdisk-test" "$root/ext4.img" write
e2fsck -fn "$root/ext4.img"
"$root/ramdisk-test" "$root/ext4.img" verify
cc -std=c11 -O2 -Wall -Wextra -Werror -I "$vendor/include" \
  -I "$root/host/include" -I kernel \
  scripts/test-lwext4-vfs.c ports/lwext4/guest_vfs.c \
  kernel/fs/vfs.c kernel/fs/lookup.c kernel/task/resources.c \
  ports/lwext4/lwext4_block.c \
  kernel/block/device.c kernel/block/ramdisk.c kernel/lib/string.c \
  "$root/host/src/liblwext4.a" -o "$root/vfs-test"
"$root/vfs-test" "$root/ext4.img"

e2fsck -fn "$root/ext4.img"
echo "[LWEXT4 RAMDISK PASS] real pinned upstream ext4 create/write/rename/link/unlink/remount/fsck"
