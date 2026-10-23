#!/bin/sh
# Unmodified original AES library and real toolchain share one layout
# across native x86-64, RV64 and AArch64. No ISA-specific guest code.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2;; esac
port=$1
work="build/external/m35-aes/$port"
root="$work/rootfs"
[ "$root" = "build/external/m35-aes/$port/rootfs" ] || exit 2
compiler="build/external/m33-tcc/$port/tcc"
make="build/external/m34-gnumake/$port/gmake"
source=build/deps/m35-aes/upstream
test -s "$compiler" && test -s "$make" &&
test -s "$source/aes.c" && test -s "$source/aes.h"
rm -rf "$root"
mkdir -p "$root/bin" "$root/tests/include" "$root/tmp"
cp "$compiler" "$root/bin/tcc"
cp "$make" "$root/bin/gmake"
sh scripts/fetch-busybox-fixture.sh "$port" "$work/busybox"
cp "$work/busybox" "$root/bin/busybox"
for applet in sh chmod echo; do ln -s busybox "$root/bin/$applet"; done
# The two upstream C files remain byte-identical, including includes.
cp "$source/aes.c" "$root/tests/aes.c"
cp "$source/aes.h" "$root/tests/aes.h"
cp tests/m35-include/*.h "$root/tests/include/"
cp tests/m35-rt.c "$root/tests/rt.c"
cp tests/m35-aes-check.c "$root/tests/check.c"
cp tests/m35-native.mk "$root/tests/Makefile"
case "$port" in
  amd64) cc=gcc ;;
  riscv64) cc=riscv64-linux-gnu-gcc ;;
  arm64) cc=aarch64-linux-gnu-gcc ;;
esac
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
  tests/native-exec-init.c -o "$root/bin/native-init"
chmod 755 "$root" "$root/bin" "$root/tests" "$root/tests/include" \
  "$root/bin/gmake" "$root/bin/tcc" "$root/bin/busybox" "$root/bin/native-init"
chmod 1777 "$root/tmp"
chmod 644 "$root/tests/"*.c "$root/tests/"*.h "$root/tests/Makefile" \
 "$root/tests/include/"*.h
touch -t 197001010000 "$root" "$root/bin" "$root/tests" \
 "$root/tests/include" "$root/tmp" \
 "$root/bin/gmake" "$root/bin/tcc" "$root/bin/busybox" "$root/bin/native-init" \
 "$root/tests/aes.c" "$root/tests/aes.h" "$root/tests/rt.c" \
 "$root/tests/check.c" "$root/tests/Makefile" "$root/tests/include/"*.h
for applet in sh chmod echo; do touch -h -t 197001010000 "$root/bin/$applet"; done
( cd "$root"
  printf '%s\n' . bin bin/gmake bin/tcc bin/busybox \
    bin/sh bin/chmod bin/echo bin/native-init \
    tests tests/include tests/include/stdint.h \
    tests/include/stddef.h tests/include/string.h \
    tests/aes.c tests/aes.h tests/rt.c tests/check.c \
    tests/Makefile tmp |
      cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
cmp "$source/aes.c" "$root/tests/aes.c"
cmp "$source/aes.h" "$root/tests/aes.h"
echo "[M35 ROOTFS] $port unchanged upstream AES + original GNU Make and TinyCC"
