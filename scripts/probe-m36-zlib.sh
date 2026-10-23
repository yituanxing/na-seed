#!/bin/sh
# One true upstream project, 12 independent .o objects + executable,
# designed to exceed the historical tmpfs 8-inode limit naturally.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in
    amd64) runner=scripts/probe-x86-busybox-parity.sh; emulator='' ;;
    riscv64) runner=scripts/probe-riscv64-busybox-matrix.sh; emulator=qemu-riscv64 ;;
    arm64) runner=scripts/probe-aarch64-busybox-matrix.sh; emulator=qemu-aarch64 ;;
    *) exit 2 ;;
esac
port=$1
compiler="build/external/m33-tcc/$port/tcc"
make="build/external/m34-gnumake/$port/gmake"
test -s "$compiler" && test -s "$make"
sh scripts/fetch-m36-zlib.sh
source=build/deps/m36-zlib/upstream
work="build/external/m36-zlib/$port"
mkdir -p "$work/include"
cp "$source/zconf.h.in" "$work/include/zconf.h"
cmp "$source/zconf.h.in" "$work/include/zconf.h"
if [ -n "$emulator" ]; then
    run_tcc() { "$emulator" "$compiler" "$@"; }
else
    run_tcc() { "$compiler" "$@"; }
fi
objs=''
for file in adler32 crc32 deflate trees zutil inflate inftrees inffast compress uncompr; do
    run_tcc -DZ_SOLO -I"$work/include" -I"$source" \
        -c "$source/$file.c" -o "$work/$file.o"
    readelf -h "$work/$file.o" | grep -q 'REL (Relocatable file)'
    objs="$objs $work/$file.o"
done
run_tcc -Itests/m35-include -c tests/m35-rt.c -o "$work/rt.o"
run_tcc -DZ_SOLO -I"$work/include" -I"$source" \
    -c tests/m36-zlib-check.c -o "$work/check.o"
readelf -h "$work/check.o" | grep -q 'REL (Relocatable file)'
# The shell splitting of $objs is intentional, each filename is fixed by
# the script; never use eval and never accept a guest-controlled pathname.
run_tcc -nostdlib -static -o "$work/zlib-check" $objs "$work/rt.o" "$work/check.o"
readelf -h "$work/zlib-check" | grep -q 'EXEC (Executable file)'
if readelf -l "$work/zlib-check" | grep -q INTERP; then
    echo "[M36 LINUX FAIL] $port wrong dynamic ELF" >&2
    exit 1
fi
if [ -n "$emulator" ]; then
    "$emulator" "$work/zlib-check" > "$work/linux-deflate.log"
else
    "$work/zlib-check" > "$work/linux-deflate.log"
fi
grep -Fxq naseed-upstream-zlib-pass "$work/linux-deflate.log"
echo "[M36 LINUX DEFLATE PASS] $port original 12 objects, real deflate/inflate and bytewise compare"
sh scripts/stage-m36-zlib-rootfs.sh "$port"
export BUSYBOX_PARITY_BINARY="$make"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m36-zlib-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
sh "$runner"
echo "[M36 NASEED ZLIB PASS] $port unmodified upstream zlib, 13 new tmpfs inodes, native GNU Make/TCC build+exec"
