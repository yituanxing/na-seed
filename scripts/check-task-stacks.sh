#!/bin/sh
# Fast CPU-independent ownership checks + assembly/context compilation.
# A successful build here is NOT proof of preemption or running QEMU.
set -eu
mkdir -p build/task-stacks
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    tests/test-task-kstack.c kernel/task.c kernel/mm/mm.c kernel/lib/string.c \
    -o build/task-stacks/test-task-kstack
build/task-stacks/test-task-kstack
for port in x86_64 riscv64 aarch64; do
    case "$port" in
        x86_64)
            flags='--target=x86_64-unknown-none-elf -mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx'
            arch=x86_64; asm=kernel/arch/x86_64/entry.S ;;
        riscv64)
            flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax'
            arch=riscv64; asm=kernel/arch/riscv64/trap.S ;;
        aarch64)
            flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only'
            arch=aarch64; asm=kernel/arch/aarch64/trap.S ;;
    esac
    for src in kernel/task.c kernel/arch/"$arch"/context.c; do
        name=${src##*/}
        clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
            -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
            -I kernel -c "$src" -o "build/task-stacks/$port-$name.o"
    done
    # ARM64 common C uses -mgeneral-regs-only; its explicit trap.S saves
    # full SIMD/FP state and therefore MUST assemble with FP enabled.
    # Keep the same split as scripts/build-aarch64-full.sh.
    case "$port" in
        aarch64) asmflags='--target=aarch64-none-elf -march=armv8-a' ;;
        *) asmflags=$flags ;;
    esac
    clang $asmflags -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
        -c "$asm" -o "build/task-stacks/$port-trap.o"
    echo "[TASK STACK TARGET PASS] $port C/assembly trap and context objects"
done
