#!/bin/sh
# Fast, offline M28 x86 IRQ ABI check. Do not confuse this with full QEMU
# timer delivery or three-ISA preemption acceptance.
set -eu
mkdir -p build/m28
flags='--target=x86_64-unknown-none-elf -mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -I kernel'
for file in kernel/arch/x86_64/cpu.c kernel/arch/x86_64/context.c \
            kernel/arch/x86_64/timer.c kernel/task/sched.c; do
    out="build/m28/$(basename "$file" .c)-$(echo "$file" | tr / _).o"
    clang $flags -c "$file" -o "$out"
done
clang $flags -c kernel/arch/x86_64/entry.S -o build/m28/x86-entry.o
nm build/m28/x86-entry.o | grep -q 'nas_x86_timer_isr'
nm build/m28/x86-entry.o | grep -q 'nas_x86_syscall_return_rip'
python3 -c 'import ast; ast.parse(open("scripts/check-m28-linux-oracle.py").read())'
echo '[M28 X86 PRELIGHT PASS] full native IRQ/SYSCALL context C+asm compiled'
