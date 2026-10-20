#!/bin/sh
# Deterministic uncompressed CPIO newc fixture containing REAL Alpine ELFs.
# The hostname is an archive file, not a syscall-specific kernel response.
set -eu
out="$1"
root=build/alpine-rootfs
rm -rf "$root"
mkdir -p "$root/bin" "$root/lib" "$root/etc" "$root/tmp"
cp build/alpine/bin/busybox "$root/bin/busybox"
cp build/alpine/lib/ld-musl-x86_64.so.1 "$root/lib/ld-musl-x86_64.so.1"
printf 'naseed\n' > "$root/etc/hostname"
chmod 755 "$root" "$root/bin" "$root/lib" "$root/etc" \
    "$root/bin/busybox" "$root/lib/ld-musl-x86_64.so.1"
chmod 644 "$root/etc/hostname"
chmod 1777 "$root/tmp" # mountpoint only; actual new files live in kernel RAM
# Genuine BusyBox applet symlinks, not duplicated executables or kernel
# pathname aliases. CPIO newc preserves links as S_IFLNK + target bytes.
applets='cat wc head yes sha256sum readlink ls grep'
for applet in $applets; do
    ln -s busybox "$root/bin/$applet"
done
touch -t 197001010000 "$root" "$root/bin" "$root/lib" "$root/etc" "$root/tmp" \
    "$root/bin/busybox" "$root/lib/ld-musl-x86_64.so.1" "$root/etc/hostname"
for applet in $applets; do
    touch -h -t 197001010000 "$root/bin/$applet"
done
# The optional second *authentic* ELF uses the pinned Debian BusyBox static
# binary already validated by the existing Debian QEMU regression.
if [ -n "${NASEED_SECOND_BUSYBOX:-}" ]; then
    test -f "$NASEED_SECOND_BUSYBOX"
    mkdir -p "$root/usr/bin"
    cp "$NASEED_SECOND_BUSYBOX" "$root/usr/bin/busybox"
    chmod 755 "$root/usr" "$root/usr/bin" "$root/usr/bin/busybox"
    touch -t 197001010000 "$root/usr" "$root/usr/bin" "$root/usr/bin/busybox"
fi
mkdir -p "$(dirname "$out")"
(
    cd "$root"
    {
        printf '%s\n' . bin bin/busybox etc etc/hostname lib lib/ld-musl-x86_64.so.1 tmp
        for applet in $applets; do printf '%s\n' "bin/$applet"; done
        if [ -n "${NASEED_SECOND_BUSYBOX:-}" ]; then
            printf '%s\n' usr usr/bin usr/bin/busybox
        fi
    } | cpio -o -H newc --quiet --owner=0:0
) > "$out.tmp"
mv "$out.tmp" "$out"
echo '[CPIO] built archive of actual Alpine ELF + fixed hostname fixture'
