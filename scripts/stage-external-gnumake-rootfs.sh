#!/bin/sh
# Original GNU Make executes real BusyBox ash recipes in NaSeed user space.
set -eu
work=build/external/gnumake/amd64
root="$work/rootfs"
test -s "$work/gmake"
test -s build/external/tcc/amd64/tcc
mkdir -p "$root/bin" "$root/tests" "$root/tmp"
cp "$work/gmake" "$root/bin/gmake"
cp build/external/tcc/amd64/tcc "$root/bin/tcc"
cp tests/tcc-first.c "$root/tests/first.c"
sh scripts/fetch-busybox-fixture.sh amd64 "$work/busybox"
cp "$work/busybox" "$root/bin/busybox"
ln -s busybox "$root/bin/sh"
# GNU Make executes simple recipes directly (no shell). Real Linux
# installations provide /bin/echo, and BusyBox's authentic echo applet
# provides that command without any custom kernel or Make special case.
ln -s busybox "$root/bin/echo"
gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/gnumake-native-init.c -o "$root/bin/make-init"
cp tests/gnumake-real.mk "$root/tests/Makefile" "$root/tests/first.c"
chmod 755 "$root" "$root/bin" "$root/tests" \
   "$root/bin/gmake" "$root/bin/busybox" "$root/bin/tcc" "$root/bin/make-init"
chmod 644 "$root/tests/Makefile" "$root/tests/first.c"
chmod 1777 "$root/tmp"
touch -t 197001010000 "$root" "$root/bin" "$root/tests" "$root/tmp" \
    "$root/bin/gmake" "$root/bin/busybox" "$root/bin/tcc" "$root/bin/make-init" \
    "$root/tests/Makefile" "$root/tests/first.c"
touch -h -t 197001010000 "$root/bin/sh"
touch -h -t 197001010000 "$root/bin/echo"
(
   cd "$root"
   printf '%s\n' . bin bin/gmake bin/busybox bin/sh bin/echo bin/tcc bin/make-init \
       tests tests/Makefile tests/first.c tmp |
     cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo '[GNUMAKE ROOTFS] exact Linux GNU Make + original Debian BusyBox ash + TinyCC ELF + C source'
