#!/bin/sh
# Reuse the static BusyBox fixture's exact rootfs entries; overlay only
# the genuine native Alpine loader and reconstruct the same CPIO format.
set -eu
[ "$#" -eq 3 ] || {
    echo 'usage: stage-alpine-native-rootfs.sh ELF LOADER WORK' >&2
    exit 2
}
binary=$1 loader=$2 work=$3
test -s "$binary" && test -s "$loader"
sh scripts/stage-busybox-rootfs.sh "$binary" "$work"
mkdir -p "$work/rootfs/lib"
cp "$loader" "$work/rootfs/lib/$(basename "$loader")"
chmod 755 "$work/rootfs/lib" "$work/rootfs/lib/$(basename "$loader")"
touch -t 197001010000 "$work/rootfs/lib" "$work/rootfs/lib/$(basename "$loader")"
(
    cd "$work/rootfs"
    find . -print | LC_ALL=C sort | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
echo "[ALPINE NATIVE ROOTFS] real BusyBox and musl loader in CPIO"
