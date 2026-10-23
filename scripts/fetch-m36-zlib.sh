#!/bin/sh
# Verified upstream zlib 1.3.1 exact commit, no edited source copies.
set -eu
revision=51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
source=build/deps/m36-zlib/upstream
mkdir -p "$source"
if [ ! -d "$source/.git" ]; then
    git -C "$source" init -q
    git -C "$source" remote add origin https://github.com/madler/zlib.git
fi
if [ "$(git -C "$source" rev-parse HEAD 2>/dev/null || :)" != "$revision" ]; then
    git -C "$source" -c protocol.version=2 fetch -q --depth=1 --no-tags origin "$revision"
    git -C "$source" checkout -q --detach FETCH_HEAD
fi
test "$(git -C "$source" rev-parse HEAD)" = "$revision"
for file in adler32.c crc32.c deflate.c trees.c zutil.c \
            inflate.c inftrees.c inffast.c \
            zlib.h zconf.h.in zutil.h deflate.h trees.h crc32.h \
            inftrees.h inflate.h inffast.h inffixed.h; do
    test -s "$source/$file"
    git -C "$source" diff --exit-code -- "$file"
done
echo "[M36 ZLIB SOURCE PASS] exact original zlib v1.3.1 commit $revision"
