#!/bin/sh
# Focused pidfd host behavior without native QEMU or signed APK staging.
set -eu
mkdir -p build/m141-pidfd
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m141-pidfd.c kernel/fs/pidfd.c kernel/fd.c \
    -o build/m141-pidfd/test
build/m141-pidfd/test
