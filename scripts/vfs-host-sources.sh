#!/bin/sh
# Source this file from host-only VFS fixtures; keep the exact production
# mount registry, devfs class adapter and namei sources linked as one unit.
# Deliberately a shell word list (paths contain no whitespace).
NAS_VFS_HOST_SOURCES='kernel/fs/vfs.c kernel/fs/vfs_providers.c kernel/fs/devfs.c kernel/drivers/core/chrdev.c kernel/block/device.c kernel/block/partition.c kernel/fs/lookup.c kernel/lib/string.c kernel/mm/heap.c scripts/inotify-vfs-test-stubs.c'
