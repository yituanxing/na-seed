#!/bin/sh
# One authenticated Alpine APK installer/catalog, no second package profile.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: stage-m83-prebuilt.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m83-source
work="build/external/m83-source/$port"
root="$work/rootfs"
test -s "$root/tests/m83-zlib.tar"
test -s "$root/usr/include/stdio.h"
test -s "$root/usr/lib/libc.a"
db="$root/lib/apk/db/installed"
test -s "$db"
for package in gcc make binutils musl-dev sed grep gawk coreutils; do
    awk -v p="$package" '$0=="P:" p {found=1} END {exit !found}' "$db" || {
        echo "[M83 SIGNED APK FAIL] $port missing original $package" >&2
        exit 1
    }
done
sha256sum "$root/usr/bin/gcc" "$root/usr/bin/make" "$root/usr/bin/as" \
          "$root/usr/bin/ar" "$root/usr/bin/ld" > "$work/m83-original-toolchain.sha256"
sha256sum build/deps/m83-zlib.tar > "$work/m83-upstream-zlib-archive.sha256"
echo "[M83 HOST STAGED] $port signed original GCC/Make/binutils + pinned upstream zlib"
