#!/bin/sh
set -eu
mkdir -p build/netlink-filter
cc -std=c11 -O2 -Wall -Wextra -Werror -DNAS_ENABLE_LWIP=1 \
   -I kernel \
   scripts/test-netlink-filter.c kernel/fs/netlink_filter.c kernel/lib/string.c \
   -o build/netlink-filter/test-netlink-filter
build/netlink-filter/test-netlink-filter
