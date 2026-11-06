#!/bin/sh
# Real Linux syscall adapter + guest usercopy/FD fixture: default AF_UNIX,
# native upstream lwIP AF_INET TCP/UDP and ROUTE/NETFILTER Netlink Files.
# Netconn wire/protocol behavior has its own genuine upstream host gate.
set -eu
mkdir -p build/lwip-foundation
common='-std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fno-builtin -Wl,--gc-sections -I scripts/m89-test-include -I kernel'
sources='scripts/test-lwip-socket-abi.c kernel/fs/file_io.c kernel/mm/usercopy.c kernel/random.c kernel/fs/eventfd.c kernel/pipe.c kernel/lib/string.c kernel/drivers/display/fbdev.c kernel/drivers/core/chrdev.c kernel/drivers/core/misc.c'
cc $common -DNAS_ENABLE_LWIP=1 $sources kernel/fs/netlink_route.c \
    kernel/fs/netlink_filter.c \
    kernel/drivers/port/net.c \
    -o build/lwip-foundation/test-socket-abi-lwip
build/lwip-foundation/test-socket-abi-lwip
