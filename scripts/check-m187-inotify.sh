#!/bin/sh
# Actual native inotify provider + Linux event wire ABI; no guest emulation.
set -eu
mkdir -p build/m187-inotify
cc -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-m187-inotify-kernel.c kernel/lib/string.c \
  -o build/m187-inotify/test
build/m187-inotify/test
cc -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin \
  -I scripts/m89-test-include -fsyntax-only \
  kernel/fs/inotify.c kernel/fs/vfs.c kernel/fs/syscall.c kernel/fs/file_io.c
