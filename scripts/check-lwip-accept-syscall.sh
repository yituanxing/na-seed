#!/bin/sh
# Focused native Linux accept/accept4 ABI and FD lease transaction test.
# The actual upstream Netconn worker is tested independently by the lwIP
# foundation; this checks the real syscall and global fd.c ownership path.
set -eu
mkdir -p build/lwip-foundation
cc -std=c11 -O0 -Wall -Wextra -Werror -Wno-unused-parameter \
   -ffunction-sections -fdata-sections -ffreestanding -fno-builtin \
   -DNAS_ENABLE_LWIP=1 -I ports/lwip/include \
   -I third_party/lwip/src/include -I kernel \
   scripts/test-lwip-accept-syscall.c kernel/fd.c \
   kernel/fs/netlink_filter.c \
   -Wl,--gc-sections -o build/lwip-foundation/test-accept-syscall
build/lwip-foundation/test-accept-syscall
