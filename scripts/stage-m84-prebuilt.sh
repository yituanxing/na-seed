#!/bin/sh
# Exactly one signed Alpine installer and two already-owned publisher fetchers.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: stage-m84-prebuilt.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sqlite_archive=$(sh scripts/fetch-m57-sqlite.sh)
lua_archive=$(sh scripts/fetch-external-lua.sh)
test -s "$sqlite_archive" && test -s "$lua_archive"
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m84-source
work="build/external/m84-source/$port"
root="$work/rootfs"
test -s "$root/tests/m84-sqlite.tar.gz" && test -s "$root/tests/m84-lua.tar.gz"
test -s "$root/usr/lib/libc.a" && test -s "$root/usr/include/stdio.h"
db="$root/lib/apk/db/installed"
test -s "$db"
for package in gcc make binutils musl-dev sed grep gawk coreutils; do
    awk -v p="$package" '$0=="P:" p {found=1} END {exit !found}' "$db" || {
        echo "[M84 SIGNED APK FAIL] $port missing original $package" >&2
        exit 1
    }
done
sha256sum "$root/usr/bin/gcc" "$root/usr/bin/make" "$root/usr/bin/as" \
          "$root/usr/bin/ar" "$root/usr/bin/ld" > "$work/m84-original-toolchain.sha256"
openssl dgst -sha3-256 "$sqlite_archive" > "$work/m84-sqlite-publisher.sha3"
sha256sum "$lua_archive" > "$work/m84-lua-publisher.sha256"
echo "[M84 HOST STAGED] $port original GCC/Make + publisher-verified SQLite3 and Lua"
