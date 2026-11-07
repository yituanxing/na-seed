#!/bin/sh
# Focused M174 host gate: production-mounted devfs namespace and genuinely
# shared File/FD/SCM_RIGHTS owners. The three QEMU ISA runs are separate and
# occur once at the final integration commit, not at each local change.
set -eu
sh -n scripts/check-m174-devfs.sh
. scripts/vfs-host-sources.sh
mkdir -p build/m174-devfs

# Retain real VFS path/mount compatibility and the production AF_UNIX rights
# backend. M107's *separate Linux-host* socket oracle remains in its own
# historical gate: repeating it here adds no devfs coverage and fails in
# restricted build environments where host AF_UNIX creation is prohibited.
sh scripts/check-vfs-provider.sh
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    scripts/test-m107-unix-kernel.c kernel/fs/unix_socket.c \
    kernel/lib/string.c -o build/m174-devfs/test-unix-rights
build/m174-devfs/test-unix-rights
# M172 includes the original device-node ASAN gates, fbdev, syscall usercopy,
# PTY/TTY, input/evdev and three-ISA input source syntax.
sh scripts/check-m172-device-display.sh

# Reuse the established VFS boot/tmpfs fixture but link the genuine mounted
# devfs provider and device class registry; no alternative path resolver.
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fno-builtin \
    -fsanitize=address,undefined -fno-omit-frame-pointer -I kernel \
    scripts/test-m174-devfs-vfs.c $NAS_VFS_HOST_SOURCES \
    -o build/m174-devfs/test-mounted-vfs
ASAN_OPTIONS=detect_leaks=0 build/m174-devfs/test-mounted-vfs

# Reuse M106's exact production syscall, usercopy, FD table and sendmsg code.
# Its controlled VFS fixture supplies a successful mounted char-inode; the
# above independently ASAN/UBSAN-tested fixture proves real VFS provider
# behavior. Keep this large syscall test's existing M106 link/GC discipline:
# sanitizing its embedded unrelated FileOps tables retains non-fixture kernel
# entrypoints and creates unresolved imports unrelated to the tested path.
cc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -fno-builtin -Wl,--gc-sections -I scripts/m89-test-include -I kernel \
    scripts/test-m174-devfs-open.c kernel/fs/file_io.c kernel/mm/usercopy.c \
    kernel/drivers/display/fbdev.c kernel/drivers/core/chrdev.c \
    kernel/drivers/core/misc.c kernel/random.c kernel/fs/eventfd.c \
    kernel/pipe.c kernel/lib/string.c -o build/m174-devfs/test-open-file
build/m174-devfs/test-open-file

# Mounting /dev as real inodes must not steal seekable-char FileOps. A prior
# regression returned ESPIPE for native fb0 pread/pwrite despite its valid
# driver operations; keep this exact production data plane under test.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I kernel -I scripts/m89-test-include \
    scripts/test-m174-devfs-fb-positional.c kernel/fs/file_io.c \
    kernel/drivers/display/fbdev.c -o build/m174-devfs/test-fb-positional
build/m174-devfs/test-fb-positional
echo '[M174 HOST PASS] mounted devfs inode and class enumeration, FD/FileOps/rdev, syscall flags, dup and SCM_RIGHTS final close, legacy host ABI'
