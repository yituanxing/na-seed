#!/bin/sh
set -eu
mkdir -p build/netlink-generic
cc -std=c11 -O2 -Wall -Wextra -Werror -DNAS_ENABLE_LWIP=1 \
   -I kernel \
   scripts/test-netlink-generic.c kernel/fs/netlink_generic.c kernel/lib/string.c \
   -o build/netlink-generic/test-netlink-generic
build/netlink-generic/test-netlink-generic
