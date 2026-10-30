#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -O2 -Wall -Wextra -Werror scripts/test-a012-linux-socket-stream.c \
    -o /tmp/naseed-a012-linux-socket-stream
/tmp/naseed-a012-linux-socket-stream
