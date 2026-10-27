#!/bin/sh
# Full RV64 kernel build: EXACT core .c translation units used by x86 M1..M9.
# Only CPU/MMU/context/trap/serial/platform provider objects are different.
# No NASEED_PORT_M0, no fake syscall/FS/exec stubs.
set -eu
make ARCH=riscv64 build/deps/limine.h
out=build/port-riscv64/full
mkdir -p "$out"
flags="--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -I build/deps -I kernel -I third_party/unikraft/include -I ports/unikraft/include -I ports/uboot/compat"
# Optional generic syscall trace in the real native kernel; no test stub.
[ "${NAS_SYSCALL_TRACE:-0}" = 1 ] && flags="$flags -DNASEED_SYSCALL_TRACE"
[ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_LWEXT4=1"
if [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ]; then
    [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] ||
      { echo '[VIRTIO] lwext4 gate required' >&2; exit 1; }
    flags="$flags -DNAS_ENABLE_VIRTIO_BLK=1"
fi
# One authoritative full-kernel core TU list for every native ISA.
# ISA-specific hardware providers remain below in the arch= selection.
common=$(grep -v '^#' scripts/full-kernel-core.sources)
arch="kernel/arch/riscv64/cpu.c kernel/arch/riscv64/mmu.c kernel/arch/riscv64/context.c kernel/arch/riscv64/signal_frame.c kernel/arch/riscv64/timer.c kernel/arch/riscv64/serial.c kernel/arch/user_pt.c kernel/mm/user_space.c kernel/mm/mmu_bridge.c kernel/platform/riscv64.c"
if [ "${NAS_ENABLE_VIRTIO_BLK:-0}" = 1 ]; then
    arch="$arch kernel/drivers/virtio/transport.c kernel/drivers/virtio/mmio.c kernel/drivers/core/bus.c kernel/drivers/port/dma.c kernel/drivers/port/block.c ports/unikraft/split_queue.c ports/uboot/blk_port.c ports/uboot/virtio_bus.c kernel/platform/virtio_mmio_qemu.c kernel/platform/riscv64_virtio.c kernel/arch/riscv64/io.c"
fi
obj=
for f in $common $arch; do
    target="$out/$(printf '%s' "$f" | tr '/' '_').o"
    clang $flags -c "$f" -o "$target"
    obj="$obj $target"
done
if [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ]; then
    sh scripts/build-lwext4-guest.sh riscv64 "$out" kernel
    obj="$obj $out/lwext4_guest.o"
fi
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
