#!/bin/sh
# Real ext4 image, original third-party lwext4, actual NaSeed VFS and backend.
# No QEMU or CMake required; compile the unchanged vendored C sources directly.
set -eu
mode=${1:-optimized}
root=build/ext4-vfs-hotpaths
mkdir -p "$root"
build_bench() {
  cc -std=c11 -O2 -D_POSIX_C_SOURCE=200809L \
    -DCONFIG_USE_DEFAULT_CFG=1 -DCONFIG_DEBUG_PRINTF=0 \
    -DCONFIG_DEBUG_ASSERT=0 -DCONFIG_HAVE_OWN_OFLAGS=1 \
    -DCONFIG_HAVE_OWN_ERRNO=0 -DCONFIG_BLOCK_DEV_CACHE_SIZE=16 \
    -I ports/lwext4 -I third_party/lwext4/include -I kernel \
    -x c "$1" -x none scripts/bench-ext4-vfs-hotpaths.c \
    kernel/fs/vfs.c kernel/fs/lookup.c kernel/task/resources.c \
    ports/lwext4/lwext4_block.c kernel/block/device.c \
    kernel/block/ramdisk.c kernel/block/partition.c \
    kernel/lib/string.c kernel/mm/heap.c third_party/lwext4/src/*.c \
    -Wl,--wrap=ext4_dir_entry_next -o "$root/bench"
}
case "$mode" in
  baseline)
    # Compile original PR#146 code from git directly through stdin. This
    # produces a true before/after comparison without modifying the vendor,
    # writing a second copy of the adapter, or adding benchmark #ifdefs.
    git show 69b5cb17:ports/lwext4/guest_vfs.c | build_bench - ;;
  optimized) build_bench ports/lwext4/guest_vfs.c ;;
  *) echo "usage: $0 [baseline|optimized]" >&2; exit 2 ;;
esac
truncate -s 32M "$root/bench-ext4.img"
mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' "$root/bench-ext4.img"
echo "[EXT4 VFS HOTPATH MODE] $mode"
"$root/bench" "$root/bench-ext4.img" "$mode"
sh scripts/check-ext4-fsck-clean.sh "$root/bench-ext4.img"
