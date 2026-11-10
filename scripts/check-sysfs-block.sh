#!/bin/sh
# Host-only regression of production block core, partition, devfs and sysfs.
set -eu
. scripts/vfs-host-sources.sh
mkdir -p build/sysfs-block
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-sysfs-block.c $NAS_VFS_HOST_SOURCES \
  -o build/sysfs-block/test
build/sysfs-block/test
