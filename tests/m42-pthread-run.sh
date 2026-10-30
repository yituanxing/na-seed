#!/bin/sh
# Original XZ Utils, original musl pthread_create/clone/futex userspace.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
xzcmd=/usr/bin/xz
[ -x "$xzcmd" ] || xzcmd=/bin/xz
"$xzcmd" --version | /bin/busybox grep -Fq 'XZ Utils'
# 4 MiB with forced 1 MiB blocks and 2 worker threads; no CPU autodetect.
"$xzcmd" --threads=2 --block-size=1048576 -0 -c \
    /tests/m42-input.bin > /tmp/m42-multi.xz
test -s /tmp/m42-multi.xz
"$xzcmd" --threads=2 -dc /tmp/m42-multi.xz > /tmp/m42-restored.bin
/bin/busybox cmp /tests/m42-input.bin /tmp/m42-restored.bin
echo M42_XZ_THREADS_PASS
echo M42_PTHREAD_PASS

# A014: run the EXACT original host-Linux oracle C source as a real target-
# ISA ELF. The original program independently asserts both ownership races:
# a suspended readv must retain its imported iovec and its original File
# after another task mutates the userspace iovec or dup2-rebinds its fd.
test -x /tests/a014-blocked-readv
for attempt in 1 2 3; do
    /tests/a014-blocked-readv > /tmp/a014-native-output.log
    /bin/busybox grep -Fxq A014_LINUX_BLOCKED_READV_IOV_SNAPSHOT_PASS /tmp/a014-native-output.log
    /bin/busybox grep -Fxq A014_LINUX_BLOCKED_READV_FILE_SNAPSHOT_PASS /tmp/a014-native-output.log
    /bin/busybox grep -Fxq A014_LINUX_TWO_BLOCKING_OWNERSHIP_RACES_PASS /tmp/a014-native-output.log
done
echo A014_NATIVE_BLOCKED_READV_IOV_SNAPSHOT_PASS
echo A014_NATIVE_BLOCKED_READV_FILE_SNAPSHOT_PASS
echo A014_NATIVE_TWO_BLOCKING_OWNERSHIP_RACES_3X_PASS
