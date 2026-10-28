#!/bin/sh
# Unmodified upstream embedded RAMFS with one kernel-only VFS adapter.
# Same freestanding objects for x86-64, RV64 and AArch64.
set -eu
test "$#" -eq 2 || { echo "usage: $0 <x86_64|riscv64|aarch64> <out>" >&2; exit 2; }
port=$1
out=$2
case "$port" in
  x86_64) triple=x86_64-unknown-none-elf
          flags="-mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx"
          machine=elf_x86_64 ;;
  riscv64) triple=riscv64-unknown-elf
           flags="-march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax"
           machine=elf64lriscv ;;
  aarch64) triple=aarch64-none-elf
           flags="-march=armv8-a -mgeneral-regs-only"
           machine=aarch64elf ;;
  *) exit 2 ;;
esac
mkdir -p "$out"
objs=""
for file in third_party/jkent_ramfs/src/ramfs_vector.c     ports/ramfs/runtime.c ports/ramfs/vfs_port.c ports/ramfs/guest_boot.c; do
  obj="$out/$(printf '%s' "$file" | tr '/' '_').o"
  clang --target="$triple" $flags -std=c11 -O2 -ffreestanding -include errno.h -fno-builtin -fno-stack-protector -fno-pic -fno-pie     -Werror=implicit-function-declaration -Werror=incompatible-pointer-types     -I ports/ramfs/freestanding -I third_party/jkent_ramfs/include     -I build/deps -I kernel -c "$file" -o "$obj"
  objs="$objs $obj"
done
ld.lld -m "$machine" -r $objs -o "$out/ramfs_guest.o"
readelf -h "$out/ramfs_guest.o" | grep -q 'Type:.*REL'
for sym in $(nm -u "$out/ramfs_guest.o" | awk 'NF {print $NF}' | sort -u); do
  case "$sym" in
    nas_heap_*|nas_vfs_*|nas_fs_lookup_node|nas_print|memcpy|memmove|memset|memcmp|strlen|strnlen|strcmp|strncmp|strchr|strrchr|strcpy|strncpy|strspn|strcspn) ;;
    *) echo "[RAMFS GUEST ERROR] unexpected kernel dependency: $sym" >&2;exit 1 ;;
  esac
done
echo "[RAMFS GUEST ELF PASS] $port unmodified upstream + shared VFS mount"
