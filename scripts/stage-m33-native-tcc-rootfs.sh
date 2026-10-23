#!/bin/sh
# The exact verified, unmodified Linux ELF compiler and C source in CPIO.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2;; esac
work="build/external/m33-tcc/$1"
root="$work/rootfs"
test -s "$work/tcc"
# Never reuse old archive contents across different guest configurations.
[ "$root" = "build/external/m33-tcc/$1/rootfs" ] || exit 2
rm -rf "$root"
mkdir -p "$root/bin" "$root/tests" "$root/tmp"
cp "$work/tcc" "$root/bin/tcc"
cp tests/tcc-first.c "$root/tests/first.c"
chmod 755 "$root" "$root/bin" "$root/tests" "$root/bin/tcc"
chmod 644 "$root/tests/first.c"
chmod 1777 "$root/tmp"
touch -t 197001010000 "$root" "$root/bin" "$root/tests" "$root/tmp" \
  "$root/bin/tcc" "$root/tests/first.c"
( cd "$root"
  printf '%s\n' . bin bin/tcc tests tests/first.c tmp |
    cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo "[M33 ROOTFS] genuine native TinyCC ELF + original CPIO source ($1)"
