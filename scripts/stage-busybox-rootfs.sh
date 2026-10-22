#!/bin/sh
# One rootfs fixture for amd64/RV64/(future)AArch64, differing ONLY in
# the ELF bytes. No arch-specific file contents, symlink lists, or clocks.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: stage-busybox-rootfs.sh ELF WORK_DIR' >&2; exit 2; }
binary=$1
work=$2
test -s "$binary"
root="$work/rootfs"
mkdir -p "$root/bin" "$root/etc" "$root/tmp"
cp "$binary" "$root/bin/busybox"
printf 'naseed\n' > "$root/etc/hostname"
chmod 755 "$root" "$root/bin" "$root/etc" "$root/bin/busybox"
chmod 644 "$root/etc/hostname"
chmod 1777 "$root/tmp"
for name in cat wc head grep ls sh mkdir rmdir rm cp mv; do
    ln -sfn busybox "$root/bin/$name"
done
touch -t 197001010000 "$root" "$root/bin" "$root/etc" "$root/tmp" \
     "$root/bin/busybox" "$root/etc/hostname"
for name in cat wc head grep ls sh mkdir rmdir rm cp mv; do
    touch -h -t 197001010000 "$root/bin/$name"
done
mkdir -p "$work"
(
    cd "$root"
    {
        printf '%s\n' . bin bin/busybox etc etc/hostname tmp
        for name in cat wc head grep ls sh mkdir rmdir rm cp mv; do
            printf '%s\n' "bin/$name"
        done
    } | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
echo "[BUSYBOX ROOTFS] one CPIO layout built for $(basename "$binary")"
