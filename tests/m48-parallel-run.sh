#!/bin/sh
set -eu
PATH=/usr/bin:/bin
export PATH
# Check GNU implementation identity; BusyBox make/xargs is insufficient.
/usr/bin/make --version | /bin/busybox grep -Fq 'GNU Make'
/usr/bin/xargs --version | /bin/busybox grep -Fq 'GNU findutils'
/usr/bin/gcc --version | /bin/busybox grep -Fq 'gcc'
echo M48_GNU_ORIGINAL_BINARIES_PASS

# GCC's assembler uses temporary files in its current directory. The real
# CPIO root is immutable, just like a read-only Linux root filesystem.
# Build from the mounted writable tmpfs; Makefile targets remain absolute.
cd /tmp

# Must be truly concurrent and preserve the original GNU jobserver behavior,
# each recipe starts a POSIX shell, invokes GNU GCC, as/ld, then exits.
/usr/bin/make -j2 -f /tests/m48-parallel.mk all
test -s /tmp/m48-alpha
test -s /tmp/m48-beta
echo M48_PARALLEL_MAKE_PASS

# GNU xargs -P2 independently exercises concurrent fork/exec/wait4;
# each child really executes two just-in-guest-linked musl ELF files.
printf 'alpha\nbeta\n' | /usr/bin/xargs -P2 -n1 /bin/sh /tests/m48-parallel-worker.sh
test -e /tmp/m48-xargs-a
test -e /tmp/m48-xargs-b
echo M48_PARALLEL_XARGS_PASS

# Re-entry must be a no-op: Make's dependency timestamps and stat semantics
# must be correct, not merely "can compile hello world".
/usr/bin/make -j2 -f /tests/m48-parallel.mk all
echo M48_INCREMENTAL_REBUILD_PASS
echo M48_NATIVE_PARALLEL_SUITE_PASS
