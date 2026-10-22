#!/bin/sh
# Self-contained CPIO: no BusyBox executable or applet required.
# Original Lua ELF and byte-identical source programs become guest files.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
port=$1
work="build/external/lua/$port"
binary="$work/lua"
root="$work/rootfs"
test -s "$binary"
mkdir -p "$root/bin" "$root/tests" "$root/tmp"
cp "$binary" "$root/bin/lua"
cp tests/lua-vm.lua tests/lua-io.lua "$root/tests/"
chmod 755 "$root" "$root/bin" "$root/tests" "$root/bin/lua"
chmod 1777 "$root/tmp"
chmod 644 "$root/tests/"*.lua
# Deterministic CPIO metadata avoids false cross-ISA fixture drift.
touch -t 197001010000 "$root" "$root/bin" "$root/bin/lua"     "$root/tests" "$root/tests/lua-vm.lua" "$root/tests/lua-io.lua" "$root/tmp"
(
    cd "$root"
    printf '%s\n' . bin bin/lua tests tests/lua-vm.lua tests/lua-io.lua tmp |
      cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo "[LUA ROOTFS] $port original ELF plus two original Lua source tests"
