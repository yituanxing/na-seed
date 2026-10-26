#!/bin/sh
set -eu
mkdir -p build/m90-io-wait
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
   -ffunction-sections -fdata-sections -Wl,--gc-sections \
   -I kernel -I scripts/m89-test-include scripts/test-m90-io-wait.c \
   kernel/fs/file_io.c kernel/fs/poll_syscall.c -o build/m90-io-wait/test
build/m90-io-wait/test
