#!/bin/sh
# Real Linux syscall adapter + guest usercopy/FD fixture: compare default
# AF_UNIX/legacy-IPv4 with opt-in native lwIP AF_INET TCP/UDP selection.
# Netconn wire/protocol behavior has its own genuine upstream host gate.
set -eu
mkdir -p build/lwip-foundation
common='-std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fno-builtin -Wl,--gc-sections -I scripts/m89-test-include -I kernel'
sources='scripts/test-lwip-socket-abi.c kernel/fs/file_io.c kernel/mm/usercopy.c kernel/random.c kernel/fs/eventfd.c kernel/pipe.c kernel/lib/string.c'
cc $common $sources -o build/lwip-foundation/test-socket-abi-legacy
build/lwip-foundation/test-socket-abi-legacy
cc $common -DNAS_ENABLE_LWIP=1 $sources kernel/fs/netlink_route.c \
    -o build/lwip-foundation/test-socket-abi-lwip
build/lwip-foundation/test-socket-abi-lwip
