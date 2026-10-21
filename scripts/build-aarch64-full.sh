#!/bin/sh
# This is NOT an AArch64 imitation of the x86 kernel: the full source
# modules for Task/ELF/exec/VM/FS/syscall/process/scheduler are identical.
# Only the CPU trap/context, MMU/PTE, console and debug-exit providers vary.
set -eu
make ARCH=aarch64 build/deps/limine.h
out=build/port-aarch64/full
mkdir -p "$out"
flags="--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -I build/deps -I kernel"
common="kernel/kernel.c kernel/init/main.c kernel/task/exec.c kernel/syscall/dispatch.c kernel/task.c kernel/task/process.c kernel/task/sched.c kernel/bootfs.c kernel/tmpfs.c kernel/fd.c kernel/path.c kernel/pipe.c kernel/boot_image.c kernel/drivers/serial.c kernel/mm/frame.c kernel/mm/vm.c kernel/mm/usercopy.c kernel/task/elf.c kernel/task/elf_segments.c kernel/task/user_stack.c kernel/task/signal.c kernel/fs/syscall.c"
arch="kernel/arch/aarch64/cpu.c kernel/arch/aarch64/mmu.c kernel/arch/aarch64/context.c kernel/arch/aarch64/debug_console.c kernel/arch/user_pt.c kernel/mm/user_space.c kernel/mm/mmu_bridge.c kernel/platform/aarch64.c"
obj=
for f in $common $arch; do
    target="$out/$(printf '%s' "$f" | tr '/' '_').o"
    clang $flags -c "$f" -o "$target"
    obj="$obj $target"
done
clang $flags -c kernel/arch/aarch64/trap.S -o "$out/trap.o"
ld.lld -m aarch64elf -nostdlib -static -z max-page-size=0x1000 \
  -T kernel/arch/aarch64/linker.ld $obj "$out/trap.o" -o "$out/kernel.elf"
clang --target=aarch64-none-elf -march=armv8-a \
  -c user/aarch64/hello.S -o "$out/hello.o"
ld.lld -m aarch64elf -nostdlib -static -z max-page-size=0x1000 \
  -T user/aarch64/linker.ld "$out/hello.o" -o "$out/user-hello.elf"
readelf -h "$out/kernel.elf" | grep -q 'Machine:.*AArch64'
readelf -h "$out/user-hello.elf" | grep -q 'Machine:.*AArch64'
echo '[PORT FULL] AArch64 the actual shared kernel and native user ELF linked'
