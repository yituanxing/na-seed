#!/bin/sh
set -eu
mkdir -p build/m103-tid
cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -fno-builtin -I kernel scripts/test-m103-thread-tid-lifecycle.c \
    kernel/task/process.c -Wl,--gc-sections -o build/m103-tid/host
build/m103-tid/host
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    scripts/test-m103-linux-clone-oracle.c -o build/m103-tid/linux-oracle
build/m103-tid/linux-oracle
cc -std=c11 -O2 -Wall -Wextra -Werror -pthread \
    scripts/test-m103-guest-tid-lifecycle.c -o build/m103-tid/guest-host
build/m103-tid/guest-host
# Three native kernel ports compile exactly this shared Task source.
# CI must fail closed if clang is absent; local host-only mode is explicit
# and NEVER emits a misleading three-target pass.
if [ "${NAS_M103_HOST_ONLY:-0}" = 1 ]; then
    echo '[M103 HOST-ONLY] three target TUs not compiled (explicit local override)'
else
    command -v clang >/dev/null 2>&1 || {
        echo '[M103 TARGET FAIL] clang unavailable (set NAS_M103_HOST_ONLY=1 for local host-only diagnostics)' >&2
        exit 1
    }
    for arch in x86_64 riscv64 aarch64; do
        case "$arch" in
          x86_64) flags='--target=x86_64-none-elf -mno-red-zone' ;;
          riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
          aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
        esac
        clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
            -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
            -c kernel/task/process.c -o "build/m103-tid/$arch-process.o"
        clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
            -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
            -c kernel/task/sched.c -o "build/m103-tid/$arch-sched.o"
        echo "[M103 TARGET PASS] $arch task process + scheduler"
    done
fi
