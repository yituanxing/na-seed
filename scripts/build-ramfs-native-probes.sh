#!/bin/sh
# Genuine writeable independent original-memory-filesystem Linux ABI test.
set -eu
exec sh scripts/build-native-vfs-probe.sh ramfs \
  user/ramfs-native-vfs.c 'RAMFS PORTABLE'
