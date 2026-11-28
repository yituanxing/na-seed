#!/bin/sh
# Reuse authentic Alpine musl/BusyBox as the long-lived user process for
# GPU IRQ/deferred scheduling acceptance. No guest-side synthetic renderer.
set -eu
[ "$#" = 1 ] || { echo "usage: sh tests/prepare-virgl-alpine.sh <x86_64|riscv64|aarch64>" >&2; exit 2; }
arch=$1
case "$arch" in x86_64|riscv64|aarch64) ;; *) exit 2 ;; esac
work="build/native-boot/$arch"
mkdir -p "$work/upstream" "$work/alpine-root"
version=3.23.7
archive="alpine-minirootfs-$version-$arch.tar.gz"
url="https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/$arch/$archive"
curl -fLsS --retry 3 -o "$work/upstream/$archive" "$url"
curl -fLsS --retry 3 -o "$work/upstream/$archive.sha256" "$url.sha256"
(cd "$work/upstream" && sha256sum -c "$archive.sha256")
tar -xzf "$work/upstream/$archive" -C "$work/alpine-root"
test -s "$work/alpine-root/bin/busybox"
test -e "$work/alpine-root/lib/ld-musl-$arch.so.1"
# Real Alpine userspace ABI verifier: install the ORIGINAL signed Python3
# package, rather than compiling a synthetic guest-side GPU executable.
# The host docker runs native apk and stages the target architecture's ELF.
docker run --rm -v "$PWD/$work/alpine-root:/target" alpine:3.23 sh -ec "
    cp -a /target/etc/apk/keys/. /etc/apk/keys/
    apk --arch $arch --root /target --keys-dir /etc/apk/keys --initdb --no-scripts \
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
        add python3
"
test -e "$work/alpine-root/usr/bin/python3"
# apk writes root-owned files from the host container. Restore ONLY staging
# ownership before unprivileged CPIO. --owner=0:0 below still writes the
# canonical root:root identities into the guest archive.
sudo rm -f "$work/alpine-root/lib/apk/db/lock"
sudo chown -R "$(id -u):$(id -g)" "$work/alpine-root"
cp tests/virtgpu-capset-abi.py "$work/alpine-root/root/virtgpu-capset-abi.py"
# Preserve upstream ELF bytes; only package their verified minirootfs into
# the existing Limine CPIO module transport used by NaSeed distro CI.
(cd "$work/alpine-root" &&
    find . -print | LC_ALL=C sort | cpio -o -H newc --quiet --owner=0:0) > "$work/alpine.cpio"
test -s "$work/alpine.cpio"
# BusyBox sleep runs unmodified upstream syscalls while the scheduler's
# existing deferred GPU service completes its real device-side IRQ/fences.
case "$arch" in
    x86_64)
        test -s build/naseed.iso && test -s build/iso/boot/kernel
        rm -rf "$work/virgl-iso"
        cp -a build/iso "$work/virgl-iso"
        rm -f "$work/virgl-iso/boot/user-hello"
        cp "$work/alpine.cpio" "$work/virgl-iso/boot/alpine.cpio"
        printf 'timeout: 0\n/NaSeed\n    protocol: limine\n    path: boot():/boot/kernel\n    cmdline: naseed.argv=/bin/busybox sleep 8\n    module_path: boot():/boot/alpine.cpio\n' > "$work/virgl-iso/boot/limine/limine.conf"
        xorriso -as mkisofs -R -r -J \
            --efi-boot boot/limine/limine-uefi-cd.bin \
            --efi-boot-part --efi-boot-image \
            -o "$work/virgl-alpine.iso" "$work/virgl-iso" >/dev/null 2>&1
        test -s "$work/virgl-alpine.iso"
        # Second independent QEMU boot uses original Python to exercise the
        # Linux DRM ioctl ABI on card0; it does not claim Mesa/GL readiness.
        sed -i 's|/bin/busybox sleep 8|/usr/bin/python3 /root/virtgpu-capset-abi.py|' \
            "$work/virgl-iso/boot/limine/limine.conf"
        xorriso -as mkisofs -R -r -J \
            --efi-boot boot/limine/limine-uefi-cd.bin \
            --efi-boot-part --efi-boot-image \
            -o "$work/virgl-caps.iso" "$work/virgl-iso" >/dev/null 2>&1
        test -s "$work/virgl-caps.iso"
        ;;
    riscv64|aarch64)
        test -s "$work/efi.img" && test -s "$work/qemu-virt.dtb"
        cp "$work/efi.img" "$work/virgl-alpine-efi.img"
        mcopy -o -i "$work/virgl-alpine-efi.img" "$work/alpine.cpio" ::/boot/alpine.cpio
        printf 'timeout: 0\n/NaSeed\n    protocol: limine\n    path: boot():/boot/kernel\n    cmdline: naseed.argv=/bin/busybox sleep 8\n    module_path: boot():/boot/alpine.cpio\n    dtb_path: boot():/boot/qemu-virt.dtb\n' > "$work/virgl-alpine-limine.conf"
        mcopy -o -i "$work/virgl-alpine-efi.img" "$work/virgl-alpine-limine.conf" ::/boot/limine/limine.conf
        test -s "$work/virgl-alpine-efi.img"
        cp "$work/virgl-alpine-efi.img" "$work/virgl-caps-efi.img"
        sed 's|/bin/busybox sleep 8|/usr/bin/python3 /root/virtgpu-capset-abi.py|' \
            "$work/virgl-alpine-limine.conf" > "$work/virgl-caps-limine.conf"
        mcopy -o -i "$work/virgl-caps-efi.img" "$work/virgl-caps-limine.conf" \
            ::/boot/limine/limine.conf
        test -s "$work/virgl-caps-efi.img"
        ;;
esac
echo "PASS: prepared upstream Alpine $arch BusyBox and signed Python3 for real GPU ABI acceptance"
