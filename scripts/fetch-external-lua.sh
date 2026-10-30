#!/bin/sh
# One SHA256-pinned official Lua 5.4.8 release tar for both the native
# Linux/QEMU-user oracle and later real NaSeed guest builds. No source patch.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: fetch-external-lua.sh' >&2; exit 2; }
version=5.4.8
sha=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
tarball="build/deps/external/lua-$version.tar.gz"
command -v curl >/dev/null
mkdir -p "$(dirname "$tarball")"
if [ ! -s "$tarball" ]; then
    temporary="$(mktemp "${tarball}.tmp.XXXXXX")"
    trap 'rm -f "$temporary"' EXIT HUP INT TERM
    curl -fLsS --retry 3 --retry-delay 2 \
        -o "$temporary" "https://www.lua.org/ftp/lua-$version.tar.gz"
    # Verify prior to atomic publication, even with parallel ISA jobs.
    printf '%s  %s\n' "$sha" "$temporary" | sha256sum -c - >&2
    mv "$temporary" "$tarball"
    trap - EXIT HUP INT TERM
fi
printf '%s  %s\n' "$sha" "$tarball" | sha256sum -c - >&2
printf '%s\n' "$tarball"
