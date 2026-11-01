#!/bin/sh
# Targeted network-stack gate: real upstream lwIP, native NaSeed kernel IPC,
# Ethernet port and every architecture's C ABI. No expensive guest QEMU here.
set -eu
test "$(git -C third_party/lwip rev-parse HEAD)" = \
    77dcd25a72509eb83f72b033d219b1d40cd8eb95
sh scripts/check-network-build-selection.sh
mkdir -p build/lwip-foundation
sources="ports/lwip/sys_arch.c ports/lwip/netif_port.c ports/lwip/network_boot.c"
upstream="third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c third_party/lwip/src/api/*.c third_party/lwip/src/netif/ethernet.c"
includes="-I ports/lwip/include -I third_party/lwip/src/include -I kernel"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fno-builtin \
   $includes scripts/test-lwip-native-sys-arch.c $sources $upstream \
   kernel/drivers/port/net.c kernel/task/k_sync.c kernel/task/kthread.c \
   kernel/task/wait_queue.c kernel/task/kernel_context.c \
   kernel/arch/x86_64/kernel_switch.S \
   -o build/lwip-foundation/test-native-netif
build/lwip-foundation/test-native-netif
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fno-builtin \
   -DNAS_ENABLE_VIRTIO_BLK=1 $includes \
   scripts/test-lwip-native-boot.c $sources $upstream \
   kernel/drivers/port/net.c kernel/task/k_sync.c kernel/task/kthread.c \
   kernel/task/wait_queue.c kernel/task/kernel_context.c \
   kernel/arch/x86_64/kernel_switch.S \
   -o build/lwip-foundation/test-native-boot
build/lwip-foundation/test-native-boot
for compiler in x86_64-linux-gnu-gcc riscv64-linux-gnu-gcc aarch64-linux-gnu-gcc; do
    if command -v "$compiler" >/dev/null 2>&1; then
        "$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
            -Wno-unused-parameter -ffreestanding -fno-builtin \
            $includes -fsyntax-only $sources $upstream
        printf '[LWIP TARGET C PASS] %s (official core+Netconn+Ethernet)\n' "$compiler"
    fi
done
