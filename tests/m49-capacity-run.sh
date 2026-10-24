#!/bin/sh
set -eu
PATH=/usr/bin:/bin
export PATH
/usr/bin/make --version | /bin/busybox grep -Fq 'GNU Make'
/usr/bin/xargs --version | /bin/busybox grep -Fq 'GNU findutils'
/usr/bin/gcc --version | /bin/busybox grep -Fq 'gcc'
echo M49_ORIGINAL_GNU_BINARIES_PASS

# CPIO is immutable, GCC creates assembler temporaries under cwd.
cd /tmp
/usr/bin/make -j4 -f /tests/m49-parallel.mk all
echo M49_FOUR_PARALLEL_COMPILERS_PASS

# A second four-way scheduler workload, independent of Make's jobserver.
printf 'alpha\nbeta\ngamma\ndelta\n' |
  /usr/bin/xargs -P4 -n1 /bin/sh /tests/m49-xargs-worker.sh
for name in alpha beta gamma delta; do
    test -e "/tmp/m49-xargs-$name-ready"
done
echo M49_FOUR_PARALLEL_XARGS_PASS

# A real guest GCC build of a process-capacity test that keeps 24 forked
# children alive simultaneously until the parent releases a pipe barrier.
/usr/bin/gcc -static /tests/m49-fork-capacity.c -o /tmp/m49-fork-capacity
/bin/busybox chmod 755 /tmp/m49-fork-capacity
/tmp/m49-fork-capacity
echo M49_24_CHILDREN_KERNEL_PASS

/usr/bin/make -j4 -f /tests/m49-parallel.mk all
echo M49_INCREMENTAL_PARALLEL_MAKE_PASS
echo M49_NATIVE_CAPACITY_SUITE_PASS
