#!/bin/sh
# Large regression-in-one: Linux builds real three-object upstream AES and
# runs NIST crypto vectors first, then NaSeed boots original Make/TinyCC
# and repeats the exact build, link, chmod and native guest exec.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
# Keep the independent host-Linux QEMU-user oracle for upstream AES vectors.
case "$port" in
  amd64) emulator='' ;;
  riscv64) emulator=qemu-riscv64 ;;
  arm64) emulator=qemu-aarch64 ;;
  *) exit 2 ;;
esac
tcc="build/external/m33-tcc/$port/tcc"
make="build/external/m34-gnumake/$port/gmake"
test -s "$tcc" && test -s "$make"
sh scripts/fetch-m35-upstream-aes.sh
source=build/deps/m35-aes/upstream
work="build/external/m35-aes/$port"
mkdir -p "$work"
if [ -n "$emulator" ]; then
  run_compiler() { "$emulator" "$tcc" "$@"; }
else
  run_compiler() { "$tcc" "$@"; }
fi
# External independent Linux/QEMU-user oracle. Same real upstream file,
# same three-object static ELF and exactly the same NIST test source.
run_compiler -Itests/m35-include -I"$source" \
 -c "$source/aes.c" -o "$work/aes.o"
run_compiler -Itests/m35-include -I"$source" \
 -c tests/m35-rt.c -o "$work/rt.o"
run_compiler -Itests/m35-include -I"$source" \
 -c tests/m35-aes-check.c -o "$work/check.o"
for name in aes rt check; do
  readelf -h "$work/$name.o" | grep -q 'REL (Relocatable file)'
done
run_compiler -nostdlib -static -o "$work/aes-check" \
 "$work/aes.o" "$work/rt.o" "$work/check.o"
readelf -h "$work/aes-check" | grep -q 'EXEC (Executable file)'
if readelf -l "$work/aes-check" | grep -q INTERP; then
  echo "[M35 LINUX FAIL] $port unexpectedly linked a dynamic interpreter" >&2
  exit 1
fi
if [ -n "$emulator" ]; then
  "$emulator" "$work/aes-check" > "$work/linux-nist.log"
else
  "$work/aes-check" > "$work/linux-nist.log"
fi
grep -Fxq naseed-upstream-aes128-pass "$work/linux-nist.log"
echo "[M35 LINUX NIST PASS] $port upstream AES128 ECB/CBC/CTR via three TinyCC objects"
sh scripts/stage-m35-aes-rootfs.sh "$port"
export BUSYBOX_PARITY_BINARY="$make"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m35-aes-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
sh "$runner"
echo "[M35 NASEED UPSTREAM AES PASS] $port 3 native objects + real GNU Make + TinyCC + paged tmpfs ELF + NIST vectors"
