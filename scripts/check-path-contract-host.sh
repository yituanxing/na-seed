#!/bin/sh
# Exact Linux PATH_MAX edge cases over the real VFS namei/provider fixture.
set -eu
mkdir -p build/path-contract
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-path-contract-host.c kernel/fs/vfs.c kernel/fs/vfs_providers.c \
  kernel/fs/lookup.c kernel/lib/string.c kernel/mm/heap.c \
  -o build/path-contract/test
build/path-contract/test
cc -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
  scripts/test-namespace-cwd-host.c kernel/fs/vfs.c kernel/fs/vfs_providers.c \
  kernel/fs/lookup.c kernel/lib/string.c kernel/mm/heap.c \
  -o build/path-contract/test-namespace
build/path-contract/test-namespace
