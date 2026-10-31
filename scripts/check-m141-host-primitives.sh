#!/bin/sh
# Narrow M141 kernel-owner checks while the native three-ISA QEMU batch is
# deliberately held until multiple software capabilities have accumulated.
set -eu
cd "$(dirname "$0")/.."

cc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -I kernel \
  kernel/ipc/sysv.c tests/m141-ipc-host.c \
  -o /tmp/naseed-m141-ipc-host-test
/tmp/naseed-m141-ipc-host-test
printf '[M141 SYSVIPC HOST PASS]\n'

cc -std=c11 -O2 -Wall -Wextra -Werror -I kernel \
  kernel/net/ipv4_loopback.c kernel/pipe.c \
  kernel/net/test_ipv4_loopback.c \
  -o /tmp/naseed-m141-ipv4-loopback-host-test
/tmp/naseed-m141-ipv4-loopback-host-test

sh scripts/check-a012-linux-socket-stream.sh
printf '[M141 HOST PRIMITIVES PASS]\n'
