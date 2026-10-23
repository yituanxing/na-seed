#!/bin/sh
# Executed by real Alpine BusyBox sh INSIDE the NaSeed kernel on three ISAs.
# Genuine upstream Makefiles, GCC, GNU ar/ld and musl. No prebuilt output.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
cd /tmp
mkdir zlib lua
cp -R /tests/src/zlib/. /tmp/zlib/
cd /tmp/zlib
# Upstream ./configure; static-only avoids asking for an unresolved runtime
# dynamic library path, while retaining normal zlib C/Make/AR semantics.
./configure --static
make -j2
test -s libz.a
# Real tmpfs ELF creation honors open+umask; executables can be chmod'ed
# explicitly in the guest just like ordinary userland install scripts.
for binary in example minigzip examplesh minigzipsh; do
    if [ -f "$binary" ]; then chmod 755 "$binary"; fi
done
make -j2 test
echo M39_ZLIB_PASS

cd /tmp
cp -R /tests/src/lua/. /tmp/lua/
cd /tmp/lua
# Upstream's native Linux target builds liblua.a, luac and lua; normal
# static executable leaves all its ordinary libc syscalls to NaSeed.
make -j2 linux MYLDFLAGS='-static -no-pie' MYLIBS='-lm -ldl'
test -s src/liblua.a
test -s src/lua
test -s src/luac
chmod 755 src/lua src/luac
./src/lua /tests/lua-vm.lua
./src/lua /tests/lua-io.lua
./src/lua -e 'print("M39_LUA_PASS")'
echo M39_BUILD_PASS
