#!/bin/sh
# Build official unmodified GNU Make 4.4.1 with native Linux libc.
set -eu
[ "${1:-amd64}" = amd64 ] || { echo 'first native GNU Make port is x86-64' >&2; exit 2; }
for tool in curl tar sha256sum gcc make readelf; do command -v "$tool" >/dev/null; done
version=4.4.1
sha=dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3
tarball="build/deps/external/make-$version.tar.gz"
work=build/external/gnumake/amd64
source="$work/make-$version"
mkdir -p "$(dirname "$tarball")" "$work"
if [ ! -s "$tarball" ]; then
    curl -fLsS --retry 3 --retry-delay 2 -o "$tarball.tmp" \
         "https://ftp.gnu.org/gnu/make/make-$version.tar.gz"
    mv "$tarball.tmp" "$tarball"
fi
printf '%s  %s\n' "$sha" "$tarball" | sha256sum -c -
if [ ! -f "$source/configure" ]; then tar -xzf "$tarball" -C "$work"; fi
test -f "$source/configure"
(
    cd "$source"
    if [ ! -f Makefile ]; then
      ./configure --prefix=/usr --disable-nls --without-guile \
          --disable-posix-spawn \
          CC=gcc CFLAGS="-O2" LDFLAGS="-static -no-pie" \
          > ../configure.log 2>&1 || { tail -80 ../configure.log >&2; exit 1; }
    fi
    make -s -j2 > ../build.log 2>&1 || {
      tail -140 ../build.log >&2; exit 1;
    }
)
test -s "$source/make"
cp "$source/make" "$work/gmake"
chmod 755 "$work/gmake"
readelf -h "$work/gmake" | grep -Fq 'Advanced Micro Devices X86-64'
readelf -h "$work/gmake" | grep -q 'EXEC (Executable file)'
if readelf -l "$work/gmake" | grep -q INTERP; then
  echo '[GNUMAKE] expected genuine static Linux ELF' >&2; exit 1
fi
"$work/gmake" --version | grep -Fq 'GNU Make 4.4.1'
# Upstream-supported, documented portable fork/exec mode; do not
# implement partial CLONE_VM/CLONE_VFORK semantics in the kernel.
grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+USE_POSIX_SPAWN[[:space:]]+1' "$source/src/config.h" && {
    echo '[GNUMAKE] posix_spawn still enabled, refusing mismatched test' >&2; exit 1;
} || :
# Run the identical, normal GNU Make binary on Linux first.
"$work/gmake" --no-print-directory -f tests/gnumake-real.mk all \
   > "$work/linux-oracle.log" 2>&1
grep -Fxq 'naseed-make-recipe-one' "$work/linux-oracle.log"
grep -Fxq 'naseed-make-recipe-two' "$work/linux-oracle.log"
sha256sum "$work/gmake" > "$work/binary.sha256"
echo '[GNUMAKE LINUX ORACLE PASS] real GNU Make parsed makefile and executed two shell recipes'
