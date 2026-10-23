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
