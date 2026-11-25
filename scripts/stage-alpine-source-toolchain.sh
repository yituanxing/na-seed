#!/bin/sh
# Native signed toolchain and pinned publisher archives; guest builds remain
# independent. The canonical overlay is the only APK installer.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1 profile=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$profile" in
    m83-source) ;;
    m84-source)
        sqlite_archive=$(sh scripts/fetch-m57-sqlite.sh)
        lua_archive=$(sh scripts/fetch-external-lua.sh)
        test -s "$sqlite_archive" && test -s "$lua_archive" ;;
    *) exit 2 ;;
esac
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" "$profile"
work="build/external/$profile/$port"
root="$work/rootfs"
label=${profile%%-*}
case "$label" in
    m83) test -s "$root/tests/m83-zlib.tar" ;;
    m84) test -s "$root/tests/m84-sqlite.tar.gz"; test -s "$root/tests/m84-lua.tar.gz" ;;
esac
test -s "$root/usr/include/stdio.h"
test -s "$root/usr/lib/libc.a"
db="$root/lib/apk/db/installed"
test -s "$db"
for package in gcc make binutils musl-dev sed grep gawk coreutils; do
    grep -Fxq "P:$package" "$db" || {
        echo "[$(printf '%s' "$label" | tr m M) SIGNED APK FAIL] $port missing original $package" >&2
        exit 1
    }
done
sha256sum "$root/usr/bin/gcc" "$root/usr/bin/make" "$root/usr/bin/as" \
          "$root/usr/bin/ar" "$root/usr/bin/ld" > "$work/$label-original-toolchain.sha256"
case "$label" in
    m83)
        sha256sum build/deps/m83-zlib.tar > "$work/m83-upstream-zlib-archive.sha256"
        echo "[M83 HOST STAGED] $port signed original GCC/Make/binutils + pinned upstream zlib" ;;
    m84)
        openssl dgst -sha3-256 "$sqlite_archive" > "$work/m84-sqlite-publisher.sha3"
        sha256sum "$lua_archive" > "$work/m84-lua-publisher.sha256"
        echo "[M84 HOST STAGED] $port original GCC/Make + publisher-verified SQLite3 and Lua" ;;
esac
