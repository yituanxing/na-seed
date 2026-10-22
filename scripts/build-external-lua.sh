#!/bin/sh
# Build untouched, release-pinned Lua with ordinary native Linux toolchains.
# No NaSeed headers, libc modifications, source patches or guest-only flags.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
port=$1
case "$port" in
  amd64) cross=; machine='Advanced Micro Devices X86-64'; emu= ;;
  riscv64) cross=riscv64-linux-gnu-; machine='RISC-V'; emu=qemu-riscv64 ;;
  arm64) cross=aarch64-linux-gnu-; machine='AArch64'; emu=qemu-aarch64 ;;
  *) exit 2 ;;
esac
cc="${cross}gcc"
command -v "$cc" >/dev/null
command -v readelf >/dev/null
command -v curl >/dev/null
if [ -n "$emu" ]; then command -v "$emu" >/dev/null; fi

version=5.4.8
sha=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
tarball="build/deps/external/lua-$version.tar.gz"
work="build/external/lua/$port"
source="$work/lua-$version"
out="$work/lua"
mkdir -p "$(dirname "$tarball")" "$work"
if [ ! -s "$tarball" ]; then
    curl -fLsS --retry 3 --retry-delay 2       -o "$tarball.tmp" "https://www.lua.org/ftp/lua-$version.tar.gz"
    mv "$tarball.tmp" "$tarball"
fi
printf '%s  %s\n' "$sha" "$tarball" | sha256sum -c -
if [ ! -f "$source/src/Makefile" ]; then
    tar -xzf "$tarball" -C "$work"
fi
test -f "$source/src/Makefile"
# Compile upstream's own native POSIX profile. Do not disable I/O, the
# allocator, VM semantics or glibc syscalls to make a guest pass.
make -s -j2 -C "$source" posix     CC="$cc -std=gnu99" AR="${cross}ar rcu" RANLIB="${cross}ranlib"     MYLDFLAGS="-static -no-pie" MYLIBS="-ldl"
cp "$source/src/lua" "$out"
chmod 755 "$out"
readelf -h "$out" | grep -Fq "$machine"
readelf -h "$out" | grep -q 'EXEC (Executable file)'
if readelf -l "$out" | grep -q INTERP; then
    echo "[LUA] nonstatic output ($port)" >&2; exit 1
fi
# The exact same upstream ELF must pass native Linux/QEMU-user first.
if [ "$port" = amd64 ]; then
    vm=$("$out" tests/lua-vm.lua)
    io=$("$out" tests/lua-io.lua)
else
    vm=$("$emu" "$out" tests/lua-vm.lua)
    io=$("$emu" "$out" tests/lua-io.lua)
fi
test "$vm" = lua-vm-ok
test "$io" = lua-io-ok
sha256sum "$out" > "$work/lua.sha256"
echo "[LUA LINUX ORACLE PASS] $port $version: VM/GC/coroutines, file read/write/remove"
