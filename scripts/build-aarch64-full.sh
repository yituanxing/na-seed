#!/bin/sh
# This is NOT an AArch64 imitation of the x86 kernel: the full source
# modules for Task/ELF/exec/VM/FS/syscall/process/scheduler are identical.
# Only the CPU trap/context, MMU/PTE, console and debug-exit providers vary.
set -eu
if [ "${NAS_ENABLE_LWIP:-1}" != 1 ]; then
    echo '[NET] the legacy AF_INET provider is retired; upstream lwIP is required' >&2
    exit 1
fi
make ARCH=aarch64 build/deps/limine.h
out=build/port-aarch64/full
mkdir -p "$out"
flags="--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -I build/deps -I kernel"
# Optional generic syscall trace in the real native kernel; no test stub.
[ "${NAS_SYSCALL_TRACE:-0}" = 1 ] && flags="$flags -DNASEED_SYSCALL_TRACE"
[ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_LWEXT4=1"
[ "${NAS_ENABLE_LITTLEFS:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_LITTLEFS=1"
[ "${NAS_ENABLE_RAMFS:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_RAMFS=1"
[ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_BLK=1 -I third_party/unikraft/include -I ports/unikraft/include -I ports/uboot/compat"
[ "${NAS_ENABLE_VIRTIO_NET:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_NET=1 -I third_party/unikraft/include -I ports/unikraft/include -I ports/uboot/compat"
[ "${NAS_ENABLE_VIRTIO_INPUT:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_INPUT=1 -I third_party/unikraft/include -I ports/unikraft/include"
[ "${NAS_ENABLE_VIRTIO_GPU:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_GPU=1 -I third_party/unikraft/include -I ports/unikraft/include"
[ "${NAS_ENABLE_VIRTIO_BLK_ASYNC:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_BLK_ASYNC=1 -I third_party/unikraft/include -I ports/unikraft/include"
if [ "${NAS_ENABLE_M179_FS_IRQ:-0}" = 1 ]; then
    [ "${NAS_ENABLE_VIRTIO_BLK_ASYNC:-0}" = 1 ] && [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] ||
      { echo '[M179] async block and lwext4 gates required' >&2; exit 1; }
    flags="$flags -DNAS_ENABLE_M179_FS_IRQ=1"
fi
if [ "${NAS_ENABLE_M180_FS_USER_IRQ:-0}" = 1 ]; then
    [ "${NAS_ENABLE_M179_FS_IRQ:-0}" = 1 ] ||
      { echo '[M180] M179 filesystem worker gate required' >&2; exit 1; }
    flags="$flags -DNAS_ENABLE_M180_FS_USER_IRQ=1"
fi
if [ "${NAS_ENABLE_M191_ALPINE_ROOT:-0}" = 1 ]; then
    [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] && [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ] ||
      { echo '[M191] physical lwext4 root requires VirtIO block' >&2; exit 1; }
    flags="$flags -DNAS_ENABLE_M191_ALPINE_ROOT=1"
fi
[ "${NAS_M241_PLATFORM_POWERDOWN:-0}" = 1 ] && flags="$flags -DNAS_M241_PLATFORM_POWERDOWN=1"
[ "${NAS_M242_PLATFORM_REBOOT:-0}" = 1 ] && flags="$flags -DNAS_M242_PLATFORM_REBOOT=1"
if [ "${NAS_ENABLE_M253_NONROOT_DAC:-0}" = 1 ]; then
    [ "${NAS_M253_DAC_ENFORCEMENT_COMPLETE:-0}" = 1 ] || {
        echo "M253 nonroot requires complete VFS DAC enforcement" >&2
        exit 2
    }
    flags="$flags -DNAS_ENABLE_M253_NONROOT_DAC=1 -DNAS_M253_DAC_ENFORCEMENT_COMPLETE=1"
fi
flags="$flags -DNAS_ENABLE_LWIP=1 -I ports/lwip/include -I third_party/lwip/src/include"
# One authoritative full-kernel core TU list for every native ISA.
# ISA-specific hardware providers remain below in the arch= selection.
common=$(grep -v '^#' scripts/full-kernel-core.sources)
if [ "${NAS_ENABLE_VIRTIO_INPUT:-0}" = 1 ]; then
    common="$common kernel/drivers/input/core.c kernel/drivers/input/evdev.c kernel/fs/input_chrdev.c"
fi
arch="kernel/arch/aarch64/cpu.c kernel/arch/aarch64/mmu.c kernel/arch/aarch64/context.c kernel/arch/aarch64/signal_frame.c kernel/arch/aarch64/timer.c kernel/arch/aarch64/debug_console.c kernel/arch/user_pt.c kernel/mm/user_space.c kernel/mm/mmu_bridge.c kernel/platform/aarch64.c"
# Only the hardware/transport source selection is target-dependent.
# VirtIO descriptors, block requests, BlockDevice and ext4 stay generic.
if [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ] || [ "${NAS_ENABLE_VIRTIO_NET:-0}" = 1 ] || [ "${NAS_ENABLE_VIRTIO_INPUT:-0}" = 1 ] || [ "${NAS_ENABLE_VIRTIO_GPU:-0}" = 1 ] || [ "${NAS_ENABLE_VIRTIO_BLK_ASYNC:-0}" = 1 ]; then
    arch="$arch kernel/drivers/virtio/transport.c kernel/drivers/virtio/mmio.c kernel/drivers/port/dma.c ports/unikraft/split_queue.c kernel/platform/virtio_mmio_qemu.c kernel/platform/aarch64_virtio.c"
fi
if [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ] || [ "${NAS_ENABLE_VIRTIO_NET:-0}" = 1 ]; then
    arch="$arch ports/uboot/virtio_compat.c"
fi
if [ "${NAS_ENABLE_VIRTIO_INPUT:-0}" = 1 ]; then
    arch="$arch kernel/drivers/input/virtio_input.c"
fi
if [ "${NAS_ENABLE_VIRTIO_GPU:-0}" = 1 ]; then
    arch="$arch kernel/drivers/display/virtio_gpu.c"
fi
if [ "${NAS_ENABLE_VIRTIO_BLK_ASYNC:-0}" = 1 ]; then
    arch="$arch kernel/block/request.c ports/uboot/async_blk.c"
fi
if [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ]; then
    [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] ||
      { echo '[VIRTIO] lwext4 gate required' >&2; exit 1; }
    arch="$arch kernel/drivers/port/block.c ports/uboot/blk_port.c ports/uboot/virtio_bus.c"
fi
if [ "${NAS_ENABLE_VIRTIO_NET:-0}" = 1 ]; then
    arch="$arch ports/uboot/net_port.c ports/uboot/virtio_net_bus.c"
fi
lwip_src="ports/lwip/libc_port.c ports/lwip/sys_arch.c ports/lwip/netif_port.c ports/lwip/network_boot.c ports/lwip/netconn_bridge.c ports/lwip/socket_port.c third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c third_party/lwip/src/api/*.c third_party/lwip/src/netif/ethernet.c"
obj=
for f in $common $arch $lwip_src; do
    target="$out/$(printf '%s' "$f" | tr '/' '_').o"
    case "$f" in ports/lwip/*|third_party/lwip/*)
        clang $flags -I ports/lwip/freestanding -c "$f" -o "$target" ;;
      *) clang $flags -c "$f" -o "$target" ;;
    esac
    obj="$obj $target"
done
if [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ]; then
    sh scripts/build-lwext4-guest.sh aarch64 "$out" kernel
    obj="$obj $out/lwext4_guest.o"
fi
if [ "${NAS_ENABLE_LITTLEFS:-0}" = 1 ]; then
    sh scripts/build-littlefs-guest.sh aarch64 "$out"
    obj="$obj $out/littlefs_guest.o"
fi
if [ "${NAS_ENABLE_RAMFS:-0}" = 1 ]; then
    sh scripts/build-ramfs-guest.sh aarch64 "$out"
    obj="$obj $out/ramfs_guest.o"
fi
# Clang -mgeneral-regs-only deliberately forbids SIMD opcodes even in
# hand-written .S. Keep it for ALL common C, but assemble the explicit
# FP/SIMD save/restore provider with the architecture's native FP ISA.
clang --target=aarch64-none-elf -march=armv8-a -g -c \
  kernel/arch/aarch64/trap.S -o "$out/trap.o"
clang --target=aarch64-none-elf -march=armv8-a -g -c \
  kernel/arch/aarch64/kernel_switch.S -o "$out/kernel_switch.o"
ld.lld -m aarch64elf -nostdlib -static -e nas_arm64_bootstrap \
  -z max-page-size=0x1000 -T kernel/arch/aarch64/linker.ld $obj "$out/trap.o" "$out/kernel_switch.o" -o "$out/kernel.elf"
clang --target=aarch64-none-elf -march=armv8-a \
  -c user/aarch64/hello.S -o "$out/hello.o"
ld.lld -m aarch64elf -nostdlib -static -z max-page-size=0x1000 \
  -T user/aarch64/linker.ld "$out/hello.o" -o "$out/user-hello.elf"
readelf -h "$out/kernel.elf" | grep -q 'Machine:.*AArch64'
readelf -h "$out/kernel.elf" | grep 'Entry point address'
readelf -Ws "$out/kernel.elf" | grep -E 'nas_arm64_bootstrap|kmain'
readelf -h "$out/user-hello.elf" | grep -q 'Machine:.*AArch64'
echo '[PORT FULL] AArch64 the actual shared kernel and native user ELF linked'
