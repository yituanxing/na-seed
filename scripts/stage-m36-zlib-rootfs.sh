#!/bin/sh
# Native binaries differ by ISA; CPIO path layout, upstream zlib sources
# and guest Makefile are identical. All ELF/.o outputs are produced in guest.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2;; esac
port=$1
source=build/deps/m36-zlib/upstream
compiler="build/external/m33-tcc/$port/tcc"
make="build/external/m34-gnumake/$port/gmake"
work="build/external/m36-zlib/$port"
root="$work/rootfs"
test -s "$compiler" && test -s "$make" && test -s "$source/zlib.h"
rm -rf "$root"
mkdir -p "$root/bin" "$root/tests" "$root/tmp"
cp "$compiler" "$root/bin/tcc"
cp "$make" "$root/bin/gmake"
sh scripts/fetch-busybox-fixture.sh "$port" "$work/busybox"
cp "$work/busybox" "$root/bin/busybox"
for applet in sh chmod; do ln -s busybox "$root/bin/$applet"; done
# Literal unmodified upstream contents; rename the *generated* zconf.h
# input only, mirroring the upstream configure-generated header naming.
for file in adler32.c crc32.c deflate.c trees.c zutil.c \
            inflate.c inftrees.c inffast.c zlib.h zutil.h \
            deflate.h trees.h crc32.h inftrees.h inflate.h \
            inffast.h inffixed.h; do
    cp "$source/$file" "$root/tests/$file"
    cmp "$source/$file" "$root/tests/$file"
done
cp "$source/zconf.h.in" "$root/tests/zconf.h"
cmp "$source/zconf.h.in" "$root/tests/zconf.h"
cp tests/m36-zlib-check.c "$root/tests/check.c"
cp tests/m36-zlib-native.mk "$root/tests/Makefile"
case "$port" in
  amd64) cc=gcc ;;
  riscv64) cc=riscv64-linux-gnu-gcc ;;
  arm64) cc=aarch64-linux-gnu-gcc ;;
esac
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/native-exec-init.c -o "$root/bin/native-init"
chmod 755 "$root" "$root/bin" "$root/tests" \
    "$root/bin/gmake" "$root/bin/tcc" "$root/bin/busybox" "$root/bin/native-init"
chmod 1777 "$root/tmp"
chmod 644 "$root/tests/"*
touch -t 197001010000 "$root" "$root/bin" "$root/tests" "$root/tmp" \
    "$root/bin/gmake" "$root/bin/tcc" "$root/bin/busybox" "$root/bin/native-init" \
    "$root/tests/"*
for applet in sh chmod; do touch -h -t 197001010000 "$root/bin/$applet"; done
(
    cd "$root"
    set -- . bin bin/gmake bin/tcc bin/busybox bin/sh bin/chmod bin/native-init tests
    for file in adler32.c crc32.c deflate.c trees.c zutil.c \
                inflate.c inftrees.c inffast.c zlib.h zutil.h \
                deflate.h trees.h crc32.h inftrees.h inflate.h \
                inffast.h inffixed.h zconf.h check.c Makefile; do
        set -- "$@" "tests/$file"
    done
    set -- "$@" tmp
    printf '%s\n' "$@" | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo "[M36 ROOTFS] $port original zlib 1.3.1 C sources, native GNU Make+TCC"
