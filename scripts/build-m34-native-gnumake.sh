#!/bin/sh
# Original GNU Make 4.4.1 cross-compiled without source patches for
# x86-64, RISC-V64 and ARM64 Linux ABI. Separate per-target build trees.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
case "$1" in
  amd64) cross=''; host=''; machine='Advanced Micro Devices X86-64'; emulate='' ;;
  riscv64) cross=riscv64-linux-gnu-; host='--host=riscv64-linux-gnu'; machine='RISC-V'; emulate=qemu-riscv64 ;;
  arm64) cross=aarch64-linux-gnu-; host='--host=aarch64-linux-gnu'; machine='AArch64'; emulate=qemu-aarch64 ;;
  *) echo "unsupported ISA $1" >&2; exit 2 ;;
esac
for tool in curl sha256sum tar make readelf "${cross}gcc"; do command -v "$tool" >/dev/null || exit 2; done
if [ -n "$emulate" ]; then command -v "$emulate" >/dev/null; fi
port=$1
work="build/external/m34-gnumake/$port"
version=4.4.1
sha=dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3
tarball="build/deps/external/make-$version.tar.gz"
source="$work/make-$version"
mkdir -p "$work" "$(dirname "$tarball")"
if [ ! -s "$tarball" ]; then
  # First try two verified mirrors; the original ftp.gnu.org may stall on
  # hosted runners. Bound every attempt instead of consuming a CI timeout.
  fetched=0
  for url in \
    "https://mirrors.kernel.org/gnu/make/make-$version.tar.gz" \
    "https://ftpmirror.gnu.org/make/make-$version.tar.gz" \
    "https://ftp.gnu.org/gnu/make/make-$version.tar.gz"; do
    rm -f "$tarball.tmp"
    if curl -fLsS --connect-timeout 8 --max-time 45 \
       --retry 1 --retry-delay 1 -o "$tarball.tmp" "$url" &&
       printf '%s  %s\n' "$sha" "$tarball.tmp" | sha256sum -c - >/dev/null; then
      mv "$tarball.tmp" "$tarball"
      fetched=1
      break
    fi
    echo "[M34] mirror unavailable or digest mismatch: $url" >&2
  done
  [ "$fetched" -eq 1 ] || { echo '[M34] all pinned GNU Make mirrors unavailable' >&2; exit 1; }
fi
printf '%s  %s\n' "$sha" "$tarball" | sha256sum -c -
if [ ! -f "$source/configure" ]; then tar -xzf "$tarball" -C "$work"; fi
(
  cd "$source"
  if [ ! -f Makefile ]; then
    ac_cv_func_vfork=no ac_cv_func_vfork_works=no \
    ./configure $host --prefix=/usr --disable-nls --without-guile \
       --disable-posix-spawn CC="${cross}gcc" CFLAGS='-O2' \
       LDFLAGS='-static -no-pie' > ../configure.log 2>&1 || {
         tail -100 ../configure.log >&2; exit 1;
       }
  fi
  make -s -j2 > ../build.log 2>&1 || {
    tail -140 ../build.log >&2; exit 1;
  }
)
out="$work/gmake"
cp "$source/make" "$out"
chmod 755 "$out"
readelf -h "$out" | grep -Fq "$machine"
readelf -h "$out" | grep -q 'EXEC (Executable file)'
if readelf -l "$out" | grep -q INTERP; then
  echo '[M34] dynamic GNU Make not permitted by current native test' >&2
  exit 1
fi
# Under upstream configure, GNU Make forks processes without requiring
# shared-VM vfork/posix_spawn shortcuts in the kernel.
test -s "$source/src/config.h"
if grep -Eq '^#define (USE_POSIX_SPAWN|HAVE_WORKING_VFORK) 1' "$source/src/config.h"; then
  echo '[M34] GNU Make unexpectedly configured shared-VM spawn' >&2
  exit 1
fi
if [ -n "$emulate" ]; then
  "$emulate" "$out" --version > "$work/linux-version.log"
else
  "$out" --version > "$work/linux-version.log"
fi
grep -Fq 'GNU Make 4.4.1' "$work/linux-version.log"
sha256sum "$out" > "$work/gmake.sha256"
echo "[M34 GNU MAKE LINUX PASS] $port real upstream 4.4.1 static native Linux ELF"
