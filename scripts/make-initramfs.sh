#!/bin/sh
# Deterministic uncompressed CPIO newc fixture containing REAL Alpine ELFs.
# The hostname is an archive file, not a syscall-specific kernel response.
set -eu
out="$1"
root=build/alpine-rootfs
rm -rf "$root"
mkdir -p "$root/bin" "$root/lib" "$root/etc"
cp build/alpine/bin/busybox "$root/bin/busybox"
cp build/alpine/lib/ld-musl-x86_64.so.1 "$root/lib/ld-musl-x86_64.so.1"
printf 'naseed\n' > "$root/etc/hostname"
chmod 755 "$root" "$root/bin" "$root/lib" "$root/etc" \
    "$root/bin/busybox" "$root/lib/ld-musl-x86_64.so.1"
chmod 644 "$root/etc/hostname"
touch -t 197001010000 "$root" "$root/bin" "$root/lib" "$root/etc" \
    "$root/bin/busybox" "$root/lib/ld-musl-x86_64.so.1" "$root/etc/hostname"
mkdir -p "$(dirname "$out")"
(
    cd "$root"
    printf '%s\n' . bin bin/busybox etc etc/hostname lib lib/ld-musl-x86_64.so.1 |
        cpio -o -H newc --quiet --owner=0:0
) > "$out.tmp"
mv "$out.tmp" "$out"
echo '[CPIO] built archive of actual Alpine ELF + fixed hostname fixture'
