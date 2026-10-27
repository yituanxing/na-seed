#!/bin/sh
# This is NOT an AArch64 imitation of the x86 kernel: the full source
# modules for Task/ELF/exec/VM/FS/syscall/process/scheduler are identical.
# Only the CPU trap/context, MMU/PTE, console and debug-exit providers vary.
set -eu
make ARCH=aarch64 build/deps/limine.h
out=build/port-aarch64/full
mkdir -p "$out"
flags="--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -I build/deps -I kernel"
# Optional generic syscall trace in the real native kernel; no test stub.
[ "${NAS_SYSCALL_TRACE:-0}" = 1 ] && flags="$flags -DNASEED_SYSCALL_TRACE"
[ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] && flags="$flags -DNAS_ENABLE_LWEXT4=1"
# One authoritative full-kernel core TU list for every native ISA.
# ISA-specific hardware providers remain below in the arch= selection.
common=$(grep -v '^#' scripts/full-kernel-core.sources)
arch="kernel/arch/aarch64/cpu.c kernel/arch/aarch64/mmu.c kernel/arch/aarch64/context.c kernel/arch/aarch64/signal_frame.c kernel/arch/aarch64/timer.c kernel/arch/aarch64/debug_console.c kernel/arch/user_pt.c kernel/mm/user_space.c kernel/mm/mmu_bridge.c kernel/platform/aarch64.c"
obj=
for f in $common $arch; do
    target="$out/$(printf '%s' "$f" | tr '/' '_').o"
    clang $flags -c "$f" -o "$target"
    obj="$obj $target"
done
if [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ]; then
    sh scripts/build-lwext4-guest.sh aarch64 "$out" kernel
    obj="$obj $out/lwext4_guest.o"
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
