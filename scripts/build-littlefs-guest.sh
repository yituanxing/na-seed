#!/bin/sh
# Same upstream sources and kernel VFS port for all three native ISAs.
# Freestanding and allocation-free inside original LittleFS.
set -eu
test "$#" -eq 2 || { echo "usage: $0 <x86_64|riscv64|aarch64> <out>" >&2; exit 2; }
target=$1
out=$2
case "$target" in
  x86_64) triple=x86_64-unknown-none-elf
          archflags="-mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx"
          emulation=elf_x86_64 ;;
  riscv64) triple=riscv64-unknown-elf
           archflags="-march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax"
           emulation=elf64lriscv ;;
  aarch64) triple=aarch64-none-elf
           archflags="-march=armv8-a -mgeneral-regs-only"
           emulation=aarch64elf ;;
  *) exit 2 ;;
esac
# The real Limine protocol header is a shared build dependency, never a
# vendored stub or per-ISA hand-written replacement.
test -s build/deps/limine.h || make build/deps/limine.h
mkdir -p "$out"
objects=""
for file in third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c \
    ports/littlefs/block_adapter.c ports/littlefs/vfs_port.c \
    ports/littlefs/guest_boot.c; do
    obj="$out/$(printf '%s' "$file" | tr '/' '_').o"
    clang --target="$triple" $archflags -O2 -std=c11 -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
        -DLFS_NO_MALLOC -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR \
        -DLFS_NO_ASSERT -Werror=implicit-function-declaration \
        -Werror=incompatible-pointer-types \
        -I ports/littlefs/freestanding/include \
        -I third_party/littlefs -I kernel -I build/deps \
        -c "$file" -o "$obj"
    objects="$objects $obj"
done
ld.lld -m "$emulation" -r $objects -o "$out/littlefs_guest.o"
readelf -h "$out/littlefs_guest.o" | grep -q 'Type:.*REL'
# A strict kernel symbol contract. All public VFS/block entrypoints are
# provided by existing common source code, never duplicates in this port.
undefined="$(nm -u "$out/littlefs_guest.o" | awk 'NF {print $NF}' | sort -u)"
for symbol in $undefined; do
    case "$symbol" in
       memcpy|memmove|memset|memcmp|strlen|strcmp|strncmp|strchr|strrchr|strcpy|strncpy|strspn|strcspn|nas_block_get|nas_block_put|nas_block_read|nas_block_write|nas_block_flush|nas_ramdisk_attach|nas_ramdisk_detach|nas_vfs_register_mount|nas_vfs_unregister_mount|nas_fs_lookup_node|nas_vfs_read_at|nas_print|nas_module) ;;
       *) echo "[LITTLEFS GUEST LINK FAIL] unexpected symbol: $symbol" >&2; exit 1 ;;
    esac
done
echo "[LITTLEFS KERNEL OBJECT PASS] $target unchanged upstream -> common VFS/BlockDevice"
