#!/bin/sh
# Independent actual upstream TinyCC binary, x86-64 first.
# Produces original Linux-ABI ELF; no NaSeed-specific compiler patches.
set -eu
[ "$#" -eq 1 ] && [ "$1" = amd64 ] || {
  echo "usage: $0 amd64 (initial x86-64 port)" >&2; exit 2;
}
for c in curl sha256sum tar make gcc readelf nm; do command -v "$c" >/dev/null; done
version=0.9.27
sha=de23af78fca90ce32dff2dd45b3432b2334740bb9bb7b05bf60fdbfc396ceb9c
tarball="build/deps/external/tcc-$version.tar.bz2"
work="build/external/tcc/amd64"
source="$work/tcc-$version"
out="$work/tcc"
mkdir -p "$(dirname "$tarball")" "$work"
if [ ! -s "$tarball" ]; then
    curl -fLsS --retry 3 --retry-delay 2 -o "$tarball.tmp"         "https://download.savannah.gnu.org/releases/tinycc/tcc-$version.tar.bz2"
    mv "$tarball.tmp" "$tarball"
fi
printf '%s  %s\n' "$sha" "$tarball" | sha256sum -c -
if [ ! -f "$source/configure" ]; then tar -xjf "$tarball" -C "$work"; fi
test -f "$source/configure"
(
    cd "$source"
    # Host GCC links TinyCC itself with genuine Linux libc, without INTERP.
    ./configure --cc=gcc --prefix=/usr --extra-ldflags="-static -no-pie"
    make -s -j2 tcc
)
cp "$source/tcc" "$out"
chmod 755 "$out"
readelf -h "$out" | grep -Fq 'Advanced Micro Devices X86-64'
readelf -h "$out" | grep -q 'EXEC (Executable file)'
if readelf -l "$out" | grep -q INTERP; then
  echo "[TCC] compiler must be a self-contained Linux static ELF" >&2; exit 1
fi
"$out" -E tests/tcc-first.c | grep -Fxq 'int naseed_answer = 42;'
"$out" -c tests/tcc-first.c -o "$work/first.o"
readelf -h "$work/first.o" | grep -q 'REL (Relocatable file)'
readelf -s "$work/first.o" | grep -Fq 'naseed_answer'
sha256sum "$out" "$work/first.o" > "$work/artifacts.sha256"
echo '[TCC LINUX ORACLE PASS] original TinyCC 0.9.27: preprocess C and emit genuine ELF .o'
