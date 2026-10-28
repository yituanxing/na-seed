#!/bin/sh
# Real BusyBox ash and Alpine GCC inside NaSeed, not a canned host binary.
# Each failed compiler stage must fail the entire guest acceptance.
set -eu
cd /tmp
. /tests/native-perf-clock.sh
echo M38_COMPILER_START
/usr/bin/gcc --version
nas_perf_begin m38
/usr/bin/gcc -static /tests/hello.c -o hello
nas_perf_phase gcc_hello
echo M38_LINKED
/bin/busybox ls -l /tmp
/bin/busybox stat /tmp/hello
/bin/busybox chmod 755 hello
./hello
nas_perf_begin m38
/usr/bin/gcc -static /tests/readv.c -o readv
nas_perf_phase gcc_readv
/bin/busybox chmod 755 readv
./readv
nas_perf_begin m38
/usr/bin/gcc -static /tests/fs-mode.c -o fs-mode
nas_perf_phase gcc_fs_mode
/bin/busybox chmod 755 fs-mode
./fs-mode
