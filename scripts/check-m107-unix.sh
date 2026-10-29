#!/bin/sh
# M107 ownership proof has three independent layers:
# (1) genuine VFS/namei/mknod provider transaction,
# (2) genuine AF_UNIX backend and SCM_RIGHTS lifecycle with controlled edges,
# (3) real Linux socket(2) semantics as an external behavioral oracle.
set -eu
cd "$(dirname "$0")/.."
sh scripts/check-vfs-provider.sh
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    scripts/test-m107-unix-kernel.c kernel/fs/unix_socket.c \
    kernel/lib/string.c -o /tmp/naseed-m107-unix-kernel
/tmp/naseed-m107-unix-kernel
cc -std=c11 -O2 -Wall -Wextra -Werror \
    scripts/test-m107-unix-linux-oracle.c -o /tmp/naseed-m107-unix-linux
/tmp/naseed-m107-unix-linux
echo '[M107 HOST PASS] actual VFS + UNIX socket and independently observed Linux ABI'
