#!/bin/sh
# Verify and execute OFFICIAL Alpine musl/BusyBox inside the real native
# NaSeed userspace. This is deliberately independent of the demo hello ELF.
# Host assembles immutable upstream CPIO; QEMU/NaSeed performs real syscalls.
set -eu
[ "$#" -eq 1 ] || { echo "usage: native-alpine.sh <riscv64|aarch64>" >&2; exit 2; }
arch=$1
case "$arch" in riscv64|aarch64) ;; *) exit 2 ;; esac
base="build/native-boot/$arch"
test -s "$base/efi.img" || {
    echo "FAIL: native-boot.sh must first create a real Limine FAT image" >&2
    exit 1
}
version=3.23.7
archive="alpine-minirootfs-$version-$arch.tar.gz"
url="https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/$arch/$archive"
mkdir -p "$base/upstream" "$base/alpine-root"
curl -fLsS --retry 3 -o "$base/upstream/$archive" "$url"
curl -fLsS --retry 3 -o "$base/upstream/$archive.sha256" "$url.sha256"
(cd "$base/upstream" && sha256sum -c "$archive.sha256")
tar -xzf "$base/upstream/$archive" -C "$base/alpine-root"
test -s "$base/alpine-root/bin/busybox"
test -e "$base/alpine-root/lib/ld-musl-$arch.so.1"
readelf -l "$base/alpine-root/bin/busybox" | grep -F "/lib/ld-musl-$arch.so.1"
# Alpine's original root filesystem is loaded as an immutable CPIO bootfs.
# The executable and musl interpreter remain genuine signed-release bytes.
(cd "$base/alpine-root" &&
    find . -print | LC_ALL=C sort | cpio -o -H newc --quiet --owner=0:0) > "$base/alpine.cpio"
test -s "$base/alpine.cpio"
cp "$base/efi.img" "$base/alpine-efi.img"
mcopy -i "$base/alpine-efi.img" "$base/alpine.cpio" "::/boot/alpine.cpio"
printf 'timeout: 0\n/NaSeed\n    protocol: limine\n    path: boot():/boot/kernel\n    cmdline: naseed.argv=/bin/busybox echo NASEED_ALPINE_%s_OK\n    module_path: boot():/boot/alpine.cpio\n    dtb_path: boot():/boot/qemu-virt.dtb\n' "$arch" > "$base/alpine-limine.conf"
mcopy -o -i "$base/alpine-efi.img" "$base/alpine-limine.conf" "::/boot/limine/limine.conf"
python3 tests/native-boot.py "$arch" "$base/alpine-efi.img" "$base/alpine-serial.log" alpine
