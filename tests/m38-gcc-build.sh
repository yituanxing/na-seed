#!/bin/sh
# Real BusyBox ash and Alpine GCC inside NaSeed, not a canned host binary.
# Each failed compiler stage must fail the entire guest acceptance.
set -eu
cd /tmp
echo M38_COMPILER_START
/usr/bin/gcc --version
/usr/bin/gcc -static /tests/hello.c -o hello
echo M38_LINKED
/bin/busybox ls -l /tmp
/bin/busybox stat /tmp/hello
/bin/busybox chmod 755 hello
./hello
/usr/bin/gcc -static /tests/readv.c -o readv
./readv
