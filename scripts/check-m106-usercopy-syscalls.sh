#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -fno-builtin -Wl,--gc-sections -I scripts/m89-test-include -I kernel \
    scripts/test-m106-usercopy-syscalls.c kernel/fs/file_io.c kernel/mm/usercopy.c \
    kernel/fs/eventfd.c kernel/pipe.c kernel/lib/string.c \
    -o /tmp/naseed-m106-usercopy-syscalls
/tmp/naseed-m106-usercopy-syscalls
sh scripts/check-a008-linux-vectors.sh
sh scripts/check-a012-linux-socket-stream.sh
