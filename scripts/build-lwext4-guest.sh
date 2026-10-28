#!/bin/sh
# Build one library against a single ISA. closed is for symbol-isolated tests;
# kernel excludes implementations already owned by common NaSeed core.
set -eu
test "$#" -eq 3 || { echo "usage: $0 <x86_64|riscv64|aarch64> <out> <closed|kernel>" >&2; exit 2; }
target=$1
out=$2
mode=$3
case "$target" in
    x86_64) triple=x86_64-unknown-none-elf
            archflags='-mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx'
            emulation=elf_x86_64 ;;
    riscv64) triple=riscv64-unknown-elf
             archflags='-march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax'
             emulation=elf64lriscv ;;
    aarch64) triple=aarch64-none-elf
             archflags='-march=armv8-a -mgeneral-regs-only'
             emulation=aarch64elf ;;
    *) echo "bad ISA: $target" >&2; exit 2 ;;
esac
case "$mode" in closed|kernel) ;; *) echo "bad link mode: $mode" >&2; exit 2 ;; esac
vendor=third_party/lwext4
port=ports/lwext4/freestanding
mkdir -p "$out"
sources="$vendor"/src/*.c
sources="$sources $port/compat.c kernel/mm/heap.c ports/lwext4/lwext4_block.c"
if [ "$mode" = kernel ]; then
    sources="$sources ports/lwext4/guest_vfs.c ports/lwext4/guest_boot.c"
fi
if [ "$mode" = closed ]; then
    sources="$sources kernel/lib/string.c kernel/block/device.c"
fi
objects=
for source in $sources; do
    obj="$out/$(printf '%s' "$source" | tr '/' '_').o"
    virt_flags=
    [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ] && virt_flags=-DNAS_ENABLE_VIRTIO_BLK=1
    clang --target="$triple" $archflags $virt_flags -std=c11 -O2 -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
        -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
        -I "$port/include" -I "$vendor/include" -I kernel -I build/deps \
        -include "$port/platform.h" -c "$source" -o "$obj"
    objects="$objects $obj"
done
ld.lld -m "$emulation" -r $objects -o "$out/lwext4_guest.o"
readelf -h "$out/lwext4_guest.o" | grep -q 'Type:.*REL'
undefined="$(nm -u "$out/lwext4_guest.o" | awk 'NF {print $NF}' | sort -u)"
if [ "$mode" = closed ]; then
    if [ -n "$undefined" ]; then
        printf '[LWEXT4 GUEST LINK FAIL] %s:\n%s\n' "$target" "$undefined" >&2
        exit 1
    fi
else
    # Strong common symbols belong to the kernel, not a duplicate in lwext4.
    for symbol in $undefined; do
        case "$symbol" in
            memcpy|memmove|memset|memcmp|strlen|strnlen|strcmp|strncmp|strchr|strrchr|strcpy|strncpy|strspn|strcspn|nas_block_get|nas_block_put|nas_block_read|nas_block_write|nas_block_flush|nas_ramdisk_attach|nas_ramdisk_detach|nas_vfs_register_mount|nas_vfs_unregister_mount|nas_fs_lookup_node|nas_vfs_join_child|nas_vfs_read_at|nas_vfs_node_size|nas_vfs_node_has_lease|nas_print|nas_module|nas_platform_virtio_blk_probe) ;;
            *) echo "[LWEXT4 KERNEL LINK FAIL] undefined: $symbol" >&2; exit 1 ;;
        esac
    done
fi
printf '[LWEXT4 GUEST LINK PASS] %s mode=%s\n' "$target" "$mode"
