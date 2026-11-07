#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -O2 -Wall -Wextra -Werror -I kernel \
  scripts/test-packet-socket.c kernel/fs/packet_socket.c \
  kernel/drivers/port/net.c -o /tmp/naseed-test-packet-socket-$$
/tmp/naseed-test-packet-socket-$$
rm -f /tmp/naseed-test-packet-socket-$$
