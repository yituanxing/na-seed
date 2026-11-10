#!/bin/sh
set -eu
. scripts/vfs-host-sources.sh
mkdir -p build/m191
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m191-root-provider.c $NAS_VFS_HOST_SOURCES \
    -o build/m191/test-root-provider
build/m191/test-root-provider
