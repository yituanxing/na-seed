#!/bin/sh
# Guest runs the original TinyCC Linux ELF, not BusyBox and not a proxy.
set -eu
[ "$#" -eq 1 ] && [ "$1" = amd64 ] || exit 2
work=build/external/tcc/amd64
root="$work/rootfs"
test -s "$work/tcc"
mkdir -p "$root/bin" "$root/tests" "$root/tmp"
cp "$work/tcc" "$root/bin/tcc"
cp tests/tcc-first.c "$root/tests/first.c"
chmod 755 "$root" "$root/bin" "$root/tests" "$root/bin/tcc"
chmod 644 "$root/tests/first.c"
chmod 1777 "$root/tmp"
touch -t 197001010000 "$root" "$root/bin" "$root/tests" "$root/tmp" "$root/bin/tcc" "$root/tests/first.c"
(
    cd "$root"
    printf '%s\n' . bin bin/tcc tests tests/first.c tmp |
      cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo '[TCC ROOTFS] native compiler + genuine C source, no BusyBox'
