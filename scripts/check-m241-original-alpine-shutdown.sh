#!/bin/sh
# Bounded, native-QEMU-free preflight for original BusyBox shutdown lifecycle.
# Do NOT promote this HOST proof to a three-ISA Alpine poweroff PASS.
set -eu
mkdir -p build/m241
sh scripts/check-m105-signal-group.sh
sh scripts/check-m191-pid1-quiesce.sh
${CC:-cc} -std=c11 -Wall -Wextra -Werror scripts/test-syscall-abi.c \
    -o build/m241/x86-abi
build/m241/x86-abi
${CC:-cc} -std=c11 -Wall -Wextra -Werror scripts/test-generic64-clone.c \
    -o build/m241/generic64-abi
build/m241/generic64-abi
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DNAS_ENABLE_LWIP=1 -I scripts/m89-test-include -I kernel \
    scripts/test-m241-reboot-dispatch.c -o build/m241/sigwait-dispatch
build/m241/sigwait-dispatch
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DNAS_ENABLE_LWIP=1 -I scripts/m89-test-include -I kernel \
    scripts/test-m149-tinyproxy-send-nosignal.c -o build/m241/m149-dispatch
build/m241/m149-dispatch
${CC:-cc} -std=c11 -Wall -Wextra -Werror -ffreestanding \
    -fno-builtin -I kernel -fsyntax-only kernel/syscall/dispatch.c
${CC:-cc} -std=c11 -Wall -Wextra -Werror -ffreestanding \
    -fno-builtin -I kernel -fsyntax-only -DNAS_ENABLE_M191_ALPINE_ROOT=1 \
    -DNAS_ENABLE_LWEXT4=1 -DNAS_ENABLE_M179_FS_IRQ=1 \
    -DNAS_ENABLE_M180_FS_USER_IRQ=1 kernel/init/main.c
python3 scripts/check-architecture-boundaries.py
python3 scripts/check-three-isa-core.py
echo '[M241 HOST PREFLIGHT PASS] actual syscall decoder, PID1 signal consumption, vfork shutdown contract, deferred ext4 teardown; native QEMU still REQUIRED'
