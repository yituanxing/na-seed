#!/bin/sh
# Shared common Linux syscall ABI probe linker: one target matrix for all FS.
set -eu
exec sh scripts/build-native-vfs-probe.sh littlefs \
  user/littlefs-native-vfs.c 'LITTLEFS PORTABLE'
