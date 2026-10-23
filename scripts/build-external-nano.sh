#!/bin/sh
# Genuine, byte-pinned GNU nano release, statically linked to native Linux.
# No editor source patches and no NaSeed-specific substitute implementation.
set -eu
[ "$#" -eq 1 ] && [ "$1" = amd64 ] || { echo "usage: $0 amd64" >&2; exit 2; }
for tool in curl sha256sum tar gcc make readelf; do command -v "$tool" >/dev/null; done
version=9.3
digest=a9affc52c9596ee0682bcdd4ceb3c0b235eb221d83d733dfd9b0dff45897cb0d
tarball="build/deps/external/nano-$version.tar.xz"
work=build/external/nano/amd64
source="$work/nano-$version"
out="$work/nano"
mkdir -p "$(dirname "$tarball")" "$work"
if [ ! -s "$tarball" ]; then
  curl -fLsS --retry 3 --retry-delay 2 -o "$tarball.tmp"     "https://www.nano-editor.org/dist/v9/nano-$version.tar.xz"
  mv "$tarball.tmp" "$tarball"
fi
printf '%s  %s\n' "$digest" "$tarball" | sha256sum -c -
if [ ! -f "$source/configure" ]; then tar -xJf "$tarball" -C "$work"; fi
test -f "$source/configure"
(
  cd "$source"
  # Upstream's documented --enable-tiny and --disable-utf8 profile.
  # This remains the actual nano editor, using actual Linux libc+ncurses.
  ./configure --prefix=/usr --enable-tiny --disable-utf8 --disable-nls     CC=gcc LDFLAGS="-static -no-pie" LIBS="-ltinfo" > ../configure.log 2>&1 || {
      tail -100 ../configure.log >&2; exit 1;
    }
  make -s -j2 > ../build.log 2>&1 || {
    tail -150 ../build.log >&2; exit 1;
  }
)
test -s "$source/src/nano"
cp "$source/src/nano" "$out"
chmod 755 "$out"
readelf -h "$out" | grep -Fq 'Advanced Micro Devices X86-64'
readelf -h "$out" | grep -q 'EXEC (Executable file)'
if readelf -l "$out" | grep -q INTERP; then
  echo '[NANO] GNU nano unexpectedly dynamically linked' >&2; exit 1
fi
"$out" --version | grep -Fq 'GNU nano'
sha256sum "$out" > "$work/nano.sha256"
echo '[NANO LINUX BUILD PASS] original GNU nano 9.3 static ELF'
