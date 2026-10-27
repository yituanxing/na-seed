#!/bin/sh
# Real guest-target compiled/linkable lwext4+BlockDevice+kernel C runtime.
# No host libc; no fake ext4 objects or simulator. This intentionally builds
# a self-contained relocatable guest component, not a booted filesystem.
set -eu
mkdir -p build/lwext4-guest
vendor=third_party/lwext4
port=ports/lwext4/freestanding
for target in x86_64 riscv64 aarch64; do
    case "$target" in
        x86_64)
            triple=x86_64-unknown-none-elf
            archflags='-mno-red-zone'
            emulation=elf_x86_64
            ;;
        riscv64)
            triple=riscv64-unknown-elf
            archflags='-march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax'
            emulation=elf64lriscv
            ;;
        aarch64)
            triple=aarch64-none-elf
            archflags='-march=armv8-a -mgeneral-regs-only'
            emulation=aarch64elf
            ;;
    esac
    out="build/lwext4-guest/$target"
    mkdir -p "$out"
    objs=
    # This is a complete, real upstream translation unit set, unchanged.
    # Keep any third-party warnings from becoming NaSeed kernel warnings.
    for source in "$vendor"/src/*.c "$port"/compat.c \
        kernel/mm/heap.c kernel/lib/string.c kernel/block/device.c \
        ports/lwext4/lwext4_block.c; do
        obj="$out/$(printf '%s' "$source" | tr '/' '_').o"
        clang --target="$triple" $archflags -std=c11 -O2 -ffreestanding \
            -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
            -Werror=implicit-function-declaration \
            -Werror=incompatible-pointer-types \
            -I "$port/include" -I "$vendor/include" -I kernel \
            -include "$port/platform.h" -c "$source" -o "$obj"
        objs="$objs $obj"
    done
    ld.lld -m "$emulation" -r $objs -o "$out/lwext4-guest.o"
    # This complete port has no dependencies on the host's libc, libgcc,
    # libstdc++, libm, or QEMU helpers. Fail for any unknown symbol.
    unresolved="$(nm -u "$out/lwext4-guest.o" | awk 'NF {print $NF}' | sort -u)"
    if [ -n "$unresolved" ]; then
        printf '%s\n%s\n' "[LWEXT4 GUEST LINK FAIL] $target unresolved:" "$unresolved" >&2
        exit 1
    fi
    readelf -h "$out/lwext4-guest.o" | grep -q 'Type:.*REL'
    printf '[LWEXT4 GUEST LINK PASS] %s full vendor library + BlockDevice + allocator: no unresolved symbols\n' "$target"
done
