#!/bin/sh
# Exact same CPIO layout for all three native ISAs. Only original Linux
# binary bytes differ. The generated ELF does not exist in the archive.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2;; esac
port=$1
work="build/external/m34-gnumake/$port"
root="$work/rootfs"
compiler="build/external/m33-tcc/$port/tcc"
test -s "$work/gmake" && test -s "$compiler"
[ "$root" = "build/external/m34-gnumake/$port/rootfs" ] || exit 2
rm -rf "$root"
mkdir -p "$root/bin" "$root/tests" "$root/tmp"
cp "$work/gmake" "$root/bin/gmake"
cp "$compiler" "$root/bin/tcc"
sh scripts/fetch-busybox-fixture.sh "$port" "$work/busybox"
cp "$work/busybox" "$root/bin/busybox"
for applet in sh echo chmod; do ln -s busybox "$root/bin/$applet"; done
cp tests/m34-portable-hello.c "$root/tests/native-hello.c"
cp tests/m34-native.mk "$root/tests/Makefile"
case "$port" in
  amd64) cc=gcc ;;
  riscv64) cc=riscv64-linux-gnu-gcc ;;
  arm64) cc=aarch64-linux-gnu-gcc ;;
esac
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
  tests/native-exec-init.c -o "$root/bin/native-init"
chmod 755 "$root" "$root/bin" "$root/tests" \
  "$root/bin/gmake" "$root/bin/tcc" "$root/bin/busybox" "$root/bin/native-init"
chmod 644 "$root/tests/Makefile" "$root/tests/native-hello.c"
chmod 1777 "$root/tmp"
touch -t 197001010000 "$root" "$root/bin" "$root/tests" "$root/tmp" \
    "$root/bin/gmake" "$root/bin/tcc" "$root/bin/busybox" \
    "$root/bin/native-init" "$root/tests/Makefile" "$root/tests/native-hello.c"
for applet in sh echo chmod; do touch -h -t 197001010000 "$root/bin/$applet"; done
( cd "$root"
  printf '%s\n' . bin bin/gmake bin/tcc bin/busybox \
    bin/sh bin/echo bin/chmod bin/native-init \
    tests tests/Makefile tests/native-hello.c tmp |
      cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
echo "[M34 ROOTFS] $port genuine GNU Make + TinyCC + Debian BusyBox ash + C source"
