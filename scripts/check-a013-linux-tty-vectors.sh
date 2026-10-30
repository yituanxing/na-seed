#!/bin/sh
# Separate Linux N_TTY oracle and genuine NaSeed FileOps/TTY production model.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -pthread \
    scripts/test-a013-linux-tty-vectors.c -o "$work/linux-tty-vectors"
timeout 15s "$work/linux-tty-vectors"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -fno-builtin -Wl,--gc-sections \
    -I scripts/m89-test-include -I kernel \
    scripts/test-a013-naseed-tty-vectors.c kernel/fs/file_io.c \
    kernel/mm/usercopy.c kernel/fs/eventfd.c kernel/pipe.c \
    kernel/lib/string.c kernel/drivers/tty.c kernel/drivers/tty_ldisc.c \
    -o "$work/naseed-tty-vectors"
timeout 15s "$work/naseed-tty-vectors"
echo '[NAS-A013 TTY READV PASS] host Linux PTY parity and production NaSeed FileOps/TTY'
