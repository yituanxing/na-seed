#!/bin/sh
# Full native kernel for RV64/AArch64. The source manifest is shared with x86;
# only the actual hardware provider, ABI and toolchain flags differ.
set -eu
[ "$#" = 1 ] || exit 2
port=$1
case "$port" in riscv64|aarch64) ;; *) exit 2 ;; esac
if [ "${NAS_ENABLE_LWIP:-1}" != 1 ]; then
    echo '[NET] the legacy AF_INET provider is retired; upstream lwIP is required' >&2
    exit 1
fi
make ARCH="$port" build/deps/limine.h
out=build/port-$port/full
mkdir -p "$out"
case "$port" in
    riscv64) flags="--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -I build/deps -I kernel -I third_party/unikraft/include -I ports/unikraft/include -I ports/uboot/compat" ;;
    aarch64) flags="--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -I build/deps -I kernel" ;;
esac
# Optional generic syscall trace in the real native kernel; no test stub.
[ "${NAS_SYSCALL_TRACE:-0}" = 1 ] && flags="$flags -DNASEED_SYSCALL_TRACE"
[ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_LWEXT4=1"
[ "${NAS_ENABLE_LITTLEFS:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_LITTLEFS=1"
[ "${NAS_ENABLE_RAMFS:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_RAMFS=1"
case "$port" in
    riscv64)
        if [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ]; then
            [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] ||
              { echo '[VIRTIO] lwext4 gate required' >&2; exit 1; }
            flags="$flags -DNAS_ENABLE_VIRTIO_BLK=1"
        fi
        if [ "${NAS_ENABLE_VIRTIO_NET:-0}" = 1 ]; then
            flags="$flags -DNAS_ENABLE_VIRTIO_NET=1 -I third_party/unikraft/include -I ports/unikraft/include -I ports/uboot/compat"
        fi
        [ "${NAS_ENABLE_VIRTIO_INPUT:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_INPUT=1"
        [ "${NAS_ENABLE_VIRTIO_GPU:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_GPU=1"
        [ "${NAS_ENABLE_VIRTIO_BLK_ASYNC:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_BLK_ASYNC=1"
        ;;
    aarch64)
        [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_BLK=1 -I third_party/unikraft/include -I ports/unikraft/include -I ports/uboot/compat"
        [ "${NAS_ENABLE_VIRTIO_NET:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_NET=1 -I third_party/unikraft/include -I ports/unikraft/include -I ports/uboot/compat"
        [ "${NAS_ENABLE_VIRTIO_INPUT:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_INPUT=1 -I third_party/unikraft/include -I ports/unikraft/include"
        [ "${NAS_ENABLE_VIRTIO_GPU:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_GPU=1 -I third_party/unikraft/include -I ports/unikraft/include"
        [ "${NAS_ENABLE_VIRTIO_BLK_ASYNC:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_VIRTIO_BLK_ASYNC=1 -I third_party/unikraft/include -I ports/unikraft/include"
        ;;
esac
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
[ "${NAS_ENABLE_M257_DEBIAN_SHUTDOWN:-0}" != 1 ] || {
    [ "${NAS_ENABLE_M191_ALPINE_ROOT:-0}" = 1 ] &&
      [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] &&
      [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ] &&
      [ "${NAS_M241_PLATFORM_POWERDOWN:-0}" = 1 ] || {
        echo '[M257] signed Debian halt requires ext4 root and physical poweroff' >&2; exit 1;
      }
    flags="$flags -DNAS_ENABLE_M257_DEBIAN_SHUTDOWN=1"
}
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
case "$port" in
    riscv64) arch="kernel/arch/riscv64/cpu.c kernel/arch/riscv64/mmu.c kernel/arch/riscv64/context.c kernel/arch/riscv64/signal_frame.c kernel/arch/riscv64/timer.c kernel/arch/riscv64/serial.c kernel/arch/user_pt.c kernel/mm/user_space.c kernel/mm/mmu_bridge.c kernel/platform/riscv64.c" ;;
    aarch64) arch="kernel/arch/aarch64/cpu.c kernel/arch/aarch64/mmu.c kernel/arch/aarch64/context.c kernel/arch/aarch64/signal_frame.c kernel/arch/aarch64/timer.c kernel/arch/aarch64/debug_console.c kernel/arch/user_pt.c kernel/mm/user_space.c kernel/mm/mmu_bridge.c kernel/platform/aarch64.c" ;;
esac
if [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ] || [ "${NAS_ENABLE_VIRTIO_NET:-0}" = 1 ] || [ "${NAS_ENABLE_VIRTIO_INPUT:-0}" = 1 ] || [ "${NAS_ENABLE_VIRTIO_GPU:-0}" = 1 ] || [ "${NAS_ENABLE_VIRTIO_BLK_ASYNC:-0}" = 1 ]; then
    arch="$arch kernel/drivers/virtio/transport.c kernel/drivers/virtio/mmio.c kernel/drivers/port/dma.c ports/unikraft/split_queue.c kernel/platform/virtio_mmio_qemu.c kernel/platform/${port}_virtio.c"
    [ "$port" != riscv64 ] || arch="$arch kernel/arch/riscv64/io.c"
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
    # AArch64 historically checks this after the other feature gates.
    [ "$port" = riscv64 ] || [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] ||
        { echo '[VIRTIO] lwext4 gate required' >&2; exit 1; }
    arch="$arch kernel/drivers/port/block.c ports/uboot/blk_port.c ports/uboot/virtio_bus.c"
fi
if [ "${NAS_ENABLE_VIRTIO_NET:-0}" = 1 ]; then
    arch="$arch ports/uboot/net_port.c ports/uboot/virtio_net_bus.c"
fi
lwip_src="ports/lwip/libc_port.c ports/lwip/sys_arch.c ports/lwip/netif_port.c ports/lwip/network_boot.c ports/lwip/netconn_bridge.c ports/lwip/socket_port.c third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c third_party/lwip/src/api/*.c third_party/lwip/src/netif/ethernet.c"
fdt_src="kernel/platform/firmware_fdt.c third_party/libfdt/fdt.c third_party/libfdt/fdt_ro.c third_party/libfdt/fdt_check.c"
obj=
for f in $common $arch $lwip_src $fdt_src; do
    target="$out/$(printf '%s' "$f" | tr '/' '_').o"
    case "$f" in ports/lwip/*|third_party/lwip/*)
        clang $flags -I ports/lwip/freestanding -c "$f" -o "$target" ;;
      third_party/tlsf/*) clang $flags -I ports/tlsf/freestanding -c "$f" -o "$target" ;;
      third_party/chacha20/*|kernel/random.c) clang $flags -I ports/chacha20/freestanding -I third_party/chacha20 -c "$f" -o "$target" ;;
      third_party/flanterm/*) clang $flags -DFLANTERM_FB_DISABLE_BUMP_ALLOC -c "$f" -o "$target" ;;
      third_party/libfdt/*|kernel/platform/firmware_fdt.c) clang $flags -I ports/fdt/freestanding -I third_party/libfdt -Wno-unused-parameter -Wno-sign-compare -c "$f" -o "$target" ;;
      *) clang $flags -c "$f" -o "$target" ;;
    esac
    obj="$obj $target"
done
if [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ]; then
    sh scripts/build-lwext4-guest.sh "$port" "$out" kernel
    obj="$obj $out/lwext4_guest.o"
fi
if [ "${NAS_ENABLE_LITTLEFS:-0}" = 1 ]; then
    sh scripts/build-littlefs-guest.sh "$port" "$out"
    obj="$obj $out/littlefs_guest.o"
fi
if [ "${NAS_ENABLE_RAMFS:-0}" = 1 ]; then
    sh scripts/build-ramfs-guest.sh "$port" "$out"
    obj="$obj $out/ramfs_guest.o"
fi
case "$port" in
    riscv64)
        clang $flags -c kernel/arch/riscv64/trap.S -o "$out/trap.o"
        clang $flags -c kernel/arch/riscv64/kernel_switch.S -o "$out/kernel_switch.o"
        ld.lld -m elf64lriscv -nostdlib -static --no-relax -z max-page-size=0x1000 \
          -T kernel/arch/riscv64/linker.ld $obj "$out/trap.o" "$out/kernel_switch.o" -o "$out/kernel.elf"
        clang --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax \
          -c user/riscv64/hello.S -o "$out/hello.o"
        ld.lld -m elf64lriscv -nostdlib -static --no-relax -z max-page-size=0x1000 \
          -T user/riscv64/linker.ld "$out/hello.o" -o "$out/user-hello.elf"
        readelf -h "$out/kernel.elf" | grep -q 'Machine:.*RISC-V'
        readelf -h "$out/user-hello.elf" | grep -q 'Machine:.*RISC-V'
        echo '[PORT FULL] RV64 one shared kernel, native ELF user process linked'
        ;;
    aarch64)
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
        ;;
esac
