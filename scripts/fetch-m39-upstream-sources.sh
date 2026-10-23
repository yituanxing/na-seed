#!/bin/sh
# Reuse the already pinned and validated upstream source provenance:
# exact git zlib 1.3.1 commit from M36 and SHA256 Lua 5.4.8 from Lua gate.
# No patched sources, cross-compiled guest binaries, or extra libc.
set -eu
sh scripts/fetch-m36-zlib.sh
version=5.4.8
sha=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
archive="build/deps/external/lua-$version.tar.gz"
source="build/deps/m39-upstream/lua-$version"
mkdir -p "$(dirname "$archive")" "$(dirname "$source")"
if [ ! -s "$archive" ]; then
    curl -fLsS --retry 3 --retry-delay 2 --connect-timeout 15 --max-time 120 \
        -o "$archive.tmp" "https://www.lua.org/ftp/lua-$version.tar.gz"
    mv "$archive.tmp" "$archive"
fi
printf '%s  %s\n' "$sha" "$archive" | sha256sum -c -
if [ ! -s "$source/src/Makefile" ]; then
    tar -xzf "$archive" -C "$(dirname "$source")"
fi
test -s "$source/Makefile" && test -s "$source/src/lua.c"
echo '[M39 SOURCES] exact original zlib 1.3.1 commit and Lua 5.4.8 release verified'
