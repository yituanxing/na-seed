#!/bin/sh
# Host-only, immutable upstream source preparation: the signed Alpine APK
# installer remains exclusively in stage-m83-prebuilt.sh.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: stage-m83-source-build.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
port=$1
sh scripts/fetch-m36-zlib.sh
source=build/deps/m36-zlib/upstream
archive=build/deps/m83-zlib.tar
test -s "$source/configure"
test -s "$source/Makefile.in"
mkdir -p build/deps
# The fetcher itself checks exact commit 51b7f2ab... and original tracked
# source bytes. git archive emits THAT Git tree, not host-edited source files.
git -C "$source" archive --format=tar --output="$(pwd)/$archive" HEAD
test -s "$archive"
tar -tf "$archive" | grep -Fxq configure
tar -tf "$archive" | grep -Fxq Makefile.in
tar -tf "$archive" | grep -Fxq test/example.c
echo "[M83 ORIGINAL SOURCE] $port pinned upstream zlib Git archive"
