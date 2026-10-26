#!/bin/sh
set -eu
mkdir -p build/m89-poll
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
   -ffunction-sections -fdata-sections -Wl,--gc-sections \
   -I kernel -I build/deps scripts/test-m89-poll.c \
   kernel/fs/file_io.c -o build/m89-poll/test
build/m89-poll/test
