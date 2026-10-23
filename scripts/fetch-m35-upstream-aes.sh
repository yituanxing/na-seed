#!/bin/sh
# Pin the exact upstream AES sources, do not vendor edited copies.
set -eu
revision=23856752fbd139da0b8ca6e471a13d5bcc99a08d
source=build/deps/m35-aes/upstream
mkdir -p "$source"
if [ ! -d "$source/.git" ]; then
  git -C "$source" init -q
  git -C "$source" remote add origin https://github.com/kokke/tiny-AES-c.git
fi
if [ "$(git -C "$source" rev-parse HEAD 2>/dev/null || :)" != "$revision" ]; then
  git -C "$source" -c protocol.version=2 fetch -q --depth=1 --no-tags origin "$revision"
  git -C "$source" checkout -q --detach FETCH_HEAD
fi
test "$(git -C "$source" rev-parse HEAD)" = "$revision"
for file in aes.c aes.h; do
  test -s "$source/$file"
  # A clean upstream file is the contract. Nothing from the kernel repo
  # shadows or rewrites the upstream AES implementation.
  git -C "$source" diff --exit-code -- "$file"
done
echo "[M35 AES SOURCE PASS] unchanged kokke/tiny-AES-c $revision"
