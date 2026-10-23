#!/bin/sh
# Genuine upstream TinyCC Linux-ABI executables, built for three native ISAs.
# The SAME pinned upstream Git commit has x86_64, ARM64 and RISC-V64 backends.
# This is deliberately separate from the existing frozen 0.9.27 x86 gate.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
case "$1" in
  amd64) cpu=x86_64; cross=''; machine='Advanced Micro Devices X86-64'; emulator='' ;;
  riscv64) cpu=riscv64; cross=riscv64-linux-gnu-; machine='RISC-V'; emulator=qemu-riscv64 ;;
  arm64) cpu=arm64; cross=aarch64-linux-gnu-; machine='AArch64'; emulator=qemu-aarch64 ;;
  *) echo "unknown ISA: $1" >&2; exit 2 ;;
esac
port=$1
# Oct 16 2026 upstream revision, including RV64 native backend and the
# static musl link fix. Never float with mob/master on CI.
revision=e23dabed154a8495f3ac1ff3cc9d04fa1c207893
source=build/deps/m33-tcc/upstream
work="build/external/m33-tcc/$port"
out="$work/tcc"
for tool in git make readelf nm sha256sum "${cross}gcc"; do
  command -v "$tool" >/dev/null || { echo "[M33] missing $tool" >&2; exit 2; }
done
if [ -n "$emulator" ]; then command -v "$emulator" >/dev/null; fi
mkdir -p "$work" "$source"
if [ ! -d "$source/.git" ]; then
  git -C "$source" init -q
  git -C "$source" remote add origin https://github.com/TinyCC/tinycc.git
fi
if [ "$(git -C "$source" rev-parse HEAD 2>/dev/null || :)" != "$revision" ]; then
  git -C "$source" -c protocol.version=2 fetch -q --depth=1 --no-tags origin "$revision"
  git -C "$source" checkout -q --detach FETCH_HEAD
fi
test "$(git -C "$source" rev-parse HEAD)" = "$revision"
test -f "$source/configure"
# Separate object directories per ISA; never reuse autoconf decisions,
# compiler intermediates, or libtcc objects between target architectures.
build="$work/source"
if [ ! -f "$build/.m33-source-sha" ] ||
   [ "$(cat "$build/.m33-source-sha")" != "$revision" ]; then
  rm -rf "$build"
  mkdir -p "$build"
  # This pinned compiler source is immutable; build output is per target.
  # Copy tracked paths instead of leaving architecture outputs in upstream.
  ( cd "$source" && git archive HEAD ) | tar -xf - -C "$build"
  printf '%s\n' "$revision" > "$build/.m33-source-sha"
fi
(
  cd "$build"
  # --cross-prefix selects genuine native GCC/ld for ARM64 or RV64;
  # --cpu selects the matching TinyCC backend. No guest-side wrappers.
  ./configure --cc=gcc --cpu="$cpu" --cross-prefix="$cross" \
    --prefix=/usr --extra-ldflags='-static -no-pie' \
    > "../configure.log" 2>&1 || {
      tail -100 ../configure.log >&2; exit 1;
    }
  # TinyCC's upstream Makefile compiles c2str.exe using target CC, but
  # immediately executes it to generate a host-side header. Cross builds
  # must use the real host GCC for this *build tool*; the compiler proper
  # and every guest ELF remain built with the selected native cross GCC.
  if [ -n "$cross" ]; then
    gcc -DC2STR conftest.c -o c2str.exe
  fi
  make -s -j2 tcc > ../build.log 2>&1 || {
    tail -120 ../build.log >&2; exit 1;
  }
)
cp "$build/tcc" "$out"
chmod 755 "$out"
readelf -h "$out" | grep -Fq "$machine"
readelf -h "$out" | grep -q 'EXEC (Executable file)'
if readelf -l "$out" | grep -q INTERP; then
  echo "[M33] unexpected dynamically linked TinyCC" >&2; exit 1
fi
runner="$out"
if [ -n "$emulator" ]; then runner="$emulator $out"; fi
# Same source, test and outputs on real Linux/target QEMU-user before
# attempting any OS-integration debugging.
$runner -E tests/tcc-first.c > "$work/linux-preprocess.log"
grep -Fxq 'int naseed_answer = 42;' "$work/linux-preprocess.log"
$runner -c tests/tcc-first.c -o "$work/first.o"
readelf -h "$work/first.o" | grep -q 'REL (Relocatable file)'
readelf -h "$work/first.o" | grep -Fq "$machine"
readelf -s "$work/first.o" | grep -Fq 'naseed_answer'
sha256sum "$out" "$work/first.o" > "$work/artifacts.sha256"
echo "[M33 LINUX ORACLE PASS] $port pinned upstream $revision: native preprocessor and ELF .o"
