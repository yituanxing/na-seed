#!/bin/sh
# Build an unmodified GNU Bash 5.2.37 linked against static musl.
set -eu
OUT="$1"
DEPS="$OUT/deps"
SRC="$DEPS/bash-5.2.37"
ARCHIVE="$DEPS/bash-5.2.37.tar.gz"
TARGET="$OUT/userspace/bash"
mkdir -p "$DEPS" "$OUT/userspace"
if [ ! -f "$ARCHIVE" ]; then
    curl -fL --retry 3 -o "$ARCHIVE.part" \
        https://ftp.gnu.org/gnu/bash/bash-5.2.37.tar.gz
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
echo "[BASH] source=GNU Bash 5.2.37"
printf '%s  %s\n' '9599b22ecd1d5787ad7d3b7bf0c59f312b3396d1e281175dd1f8a4014da621ff' "$ARCHIVE" | sha256sum -c -
if [ ! -f "$SRC/config.status" ]; then
    tar -xzf "$ARCHIVE" -C "$DEPS"
    ( cd "$SRC" &&
      CC=musl-gcc CFLAGS='-O2 -g' LDFLAGS='-static' \
      ./configure --enable-static-link --without-bash-malloc --disable-nls \
        --prefix=/ )
fi
make -C "$SRC" -j2
cp "$SRC/bash" "$TARGET"
readelf -h "$TARGET" | grep -q 'EXEC (Executable file)'
if readelf -l "$TARGET" | grep -q 'INTERP'; then
    echo "ERROR: GNU Bash unexpectedly requires a dynamic interpreter" >&2
    exit 1
fi
echo "[BASH] Linux-host smoke:"
"$TARGET" --noprofile --norc -c 'echo hello'
test "$("$TARGET" --noprofile --norc -c 'echo hello')" = hello
echo "[BASH] static Bash host test PASSED"
