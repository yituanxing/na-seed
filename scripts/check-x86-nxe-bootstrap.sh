#!/bin/sh
# Verifies source/flag contract plus compilation of the real x86 CPU TU.
set -eu
python3 scripts/test-x86-nxe-bootstrap.py
mkdir -p build/x86-nxe
cc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
    -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mno-red-zone \
    -I kernel -c kernel/arch/x86_64/cpu.c -o build/x86-nxe/cpu.o
objdump -d build/x86-nxe/cpu.o | grep -Fq 'cpuid'
objdump -d build/x86-nxe/cpu.o | grep -Fq 'rdmsr'
objdump -d build/x86-nxe/cpu.o | grep -Fq 'wrmsr'
echo '[X86 NXE OBJECT PASS] real production CPU object has CPUID/MSR ops'
