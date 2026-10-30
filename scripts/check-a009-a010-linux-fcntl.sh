#!/bin/sh
# Native Linux oracle; the identical source is staged into M42 original
# pthread guests, where it runs under the genuine NaSeed kernel.
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    scripts/test-a009-a010-linux-fcntl.c -o /tmp/naseed-a009-a010-linux-fcntl
/tmp/naseed-a009-a010-linux-fcntl
echo '[A009/A010 LINUX ORACLE PASS] native fcntl + positional append'
