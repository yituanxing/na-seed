#!/bin/sh
# Targeted network-stack gate: real upstream lwIP, native NaSeed kernel IPC,
# Ethernet port and every architecture's C ABI. No expensive guest QEMU here.
set -eu
test "$(git -C third_party/lwip rev-parse HEAD)" = \
    77dcd25a72509eb83f72b033d219b1d40cd8eb95
sh scripts/check-network-build-selection.sh
# Run the actual Linux Socket usercopy/FD ABI in both default and lwIP
# configurations; protocol traffic is separately exercised below.
sh scripts/check-lwip-socket-abi.sh
# Original dnsmasq's Linux NETLINK_ROUTE IPv4 interface enumeration must
# return actual lwIP loopback/DHCP addresses and correct multipart framing.
cc -std=c11 -O2 -Wall -Wextra -Werror -DNAS_ENABLE_LWIP=1 \
   -I kernel -I ports/lwip/include -I third_party/lwip/src/include \
   scripts/test-netlink-route.c kernel/fs/netlink_route.c kernel/lib/string.c \
   -o build/lwip-foundation/test-netlink-route
build/lwip-foundation/test-netlink-route
cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections \
   -fdata-sections -Wl,--gc-sections -DNAS_ENABLE_LWIP=1 \
   -I scripts/m89-test-include -I kernel \
   scripts/test-lwip-peek-wait.c \
   -o build/lwip-foundation/test-peek-wait
build/lwip-foundation/test-peek-wait
# Tinyproxy's send(MSG_NOSIGNAL) must enter the parked File writer rather
# than bypassing backpressure and exposing internal EOPNOTSUPP to the guest.
cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections \
   -fdata-sections -Wl,--gc-sections -DNAS_ENABLE_LWIP=1 \
   -I scripts/m89-test-include -I kernel \
   scripts/test-m149-tinyproxy-send-nosignal.c \
   -o build/lwip-foundation/test-tinyproxy-send-nosignal
build/lwip-foundation/test-tinyproxy-send-nosignal
# Exercise native asynchronous accept publication and EINTR cancellation
# through the actual syscall/usercopy/FD lease adapter, not a fake FD map.
sh scripts/check-lwip-accept-syscall.sh
mkdir -p build/lwip-foundation
sources="ports/lwip/sys_arch.c ports/lwip/netif_port.c ports/lwip/network_boot.c ports/lwip/netconn_bridge.c"
target_sources="$sources ports/lwip/socket_port.c"
upstream="third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c third_party/lwip/src/api/*.c third_party/lwip/src/netif/ethernet.c"
includes="-I ports/lwip/include -I third_party/lwip/src/include -I kernel"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
   -ffreestanding -fno-builtin $includes -fsyntax-only \
   ports/lwip/socket_port.c
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
   kernel/drivers/port/net.c kernel/drivers/core/bus.c \
   kernel/task/k_sync.c kernel/task/kthread.c \
   kernel/task/wait_queue.c kernel/task/kernel_context.c \
   kernel/arch/x86_64/kernel_switch.S \
   -o build/lwip-foundation/test-native-boot
build/lwip-foundation/test-native-boot
# Netconn may park the native worker while waiting for official tcpip_thread;
# the caller is a non-native syscall-like context and only collects results.
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fno-builtin \
   $includes scripts/test-lwip-netconn-bridge.c \
   ports/lwip/sys_arch.c ports/lwip/netconn_bridge.c $upstream \
   kernel/task/k_sync.c kernel/task/kthread.c \
   kernel/task/wait_queue.c kernel/task/kernel_context.c \
   kernel/arch/x86_64/kernel_switch.S \
   -o build/lwip-foundation/test-netconn-bridge
build/lwip-foundation/test-netconn-bridge
# Socket data/control provider stays on the shared OpenFile identity.
# Run a separate genuine lwIP TCP/UDP host test, without a fake Netconn.
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fno-builtin \
   $includes scripts/test-lwip-socket-port.c \
   ports/lwip/sys_arch.c ports/lwip/netconn_bridge.c \
   ports/lwip/socket_port.c $upstream \
   kernel/task/k_sync.c kernel/task/kthread.c \
   kernel/task/wait_queue.c kernel/task/kernel_context.c \
   kernel/arch/x86_64/kernel_switch.S \
   -o build/lwip-foundation/test-socket-port
build/lwip-foundation/test-socket-port
# A native ACCEPT worker must pin its destination OpenFile slot against
# competing Task allocations until the accepted Netconn can be published.
cc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
   -I kernel scripts/test-fd-slot-lease.c kernel/fd.c \
   -o build/lwip-foundation/test-fd-slot-lease
build/lwip-foundation/test-fd-slot-lease
for compiler in x86_64-linux-gnu-gcc riscv64-linux-gnu-gcc aarch64-linux-gnu-gcc; do
    if command -v "$compiler" >/dev/null 2>&1; then
        # Some build hosts provide a bare cross-GCC but no target libc
        # headers (e.g. missing bits/wordsize.h). That is not a source
        # regression; label the unavailable target syntax gate explicitly.
        if ! printf '#include <inttypes.h>\n' |
             "$compiler" -x c -fsyntax-only - >/dev/null 2>&1; then
            printf '[LWIP TARGET C SKIP] %s (target libc headers unavailable)\n' "$compiler"
            continue
        fi
        "$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
            -Wno-unused-parameter -ffreestanding -fno-builtin \
            $includes -fsyntax-only $target_sources $upstream
        printf '[LWIP TARGET C PASS] %s (official core+Netconn+Ethernet)\n' "$compiler"
    fi
done
