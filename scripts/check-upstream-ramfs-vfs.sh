#!/bin/sh
# One focused host acceptance, then three native ISA freestanding links.
set -eu
mkdir -p build/upstream-ramfs
test "$(git hash-object third_party/jkent_ramfs/src/ramfs_vector.c)" = "4b5728b58bf6024f0396ba1626c03a338b7eba1f"
test "$(git hash-object third_party/jkent_ramfs/include/ramfs/ramfs.h)" = "b63df2285eb32cbe39554331e2c535cb0717c79a"
test "$(git hash-object third_party/jkent_ramfs/LICENSE.txt)" = "be2cc4dfb609fb6c38f6365ec345bded3350dd63"
cc -D_GNU_SOURCE -std=c11 -O2 -Wall -Wextra -include errno.h -Werror=implicit-function-declaration -Werror=incompatible-pointer-types   -fno-builtin -iquote kernel -I third_party/jkent_ramfs/include   scripts/test-upstream-ramfs-vfs.c   kernel/fs/vfs.c kernel/fs/vfs_providers.c kernel/fs/lookup.c   kernel/lib/string.c ports/ramfs/vfs_port.c   third_party/jkent_ramfs/src/ramfs_vector.c   -o build/upstream-ramfs/test
build/upstream-ramfs/test
# Link the real vendor and adapter on all three ISAs: no mocked VFS API.
if command -v clang >/dev/null 2>&1;then
  for port in x86_64 riscv64 aarch64;do
    sh scripts/build-ramfs-guest.sh "$port" "build/upstream-ramfs/$port"
  done
fi
