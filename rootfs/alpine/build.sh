#!/bin/sh
# Build Alpine's original native Linux userspace on the HOST, like NaOS.
# The target architecture is metadata for apk; foreign binaries never execute.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in
    amd64) arch=x86_64; machine='Advanced Micro Devices X86-64' ;;
    riscv64) arch=riscv64; machine='RISC-V' ;;
    arm64) arch=aarch64; machine='AArch64' ;;
    *) exit 2 ;;
esac
port=$1
work="build/external/m38-gcc/$port"
root="$work/rootfs"
# Staged trees may contain root-owned APK metadata from Docker.
if [ -e "$root" ]; then sudo rm -rf "$root"; fi
mkdir -p "$root"
command -v docker >/dev/null
command -v cpio >/dev/null
command -v readelf >/dev/null
target="$(pwd)/$root"
packages="$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' rootfs/alpine/packages.txt)"
# Use Alpine's own keyring. No --allow-untrusted; disable foreign package
# install scripts instead of executing incompatible target-ISA binaries.
docker run --rm --volume "$target:/target" \
    --env APK_ARCH="$arch" --env APK_PACKAGES="$packages" \
    alpine:3.23 sh -ec '
        apk --arch "$APK_ARCH" --root /target \
            --keys-dir /etc/apk/keys --initdb --no-scripts \
            -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
            add $APK_PACKAGES
    '
sudo chown -R "$(id -u):$(id -g)" "$root"
mkdir -p "$root/tmp" "$root/tests"
chmod 1777 "$root/tmp"
cp tests/m38-gcc-hello.c "$root/tests/hello.c"
test -s "$root/usr/include/stdio.h"
test -s "$root/usr/include/stdlib.h"
test -s "$root/usr/lib/libc.a"
test -e "$root/usr/bin/gcc"
test -e "$root/usr/bin/as"
test -e "$root/usr/bin/ld"
test -e "$root/bin/busybox"
readelf -h "$root/usr/bin/gcc" | grep -Fq "$machine"
# Original layout, no handcrafted sysroot: /usr/include, /usr/lib,
# /usr/libexec/gcc, etc. Exclude device nodes, retain symlinks.
(
    cd "$root"
    find . \( -type f -o -type d -o -type l \) -print |
        LC_ALL=C sort |
        while IFS= read -r path; do
            [ "${#path}" -le 254 ] || {
                echo "[M38] path exceeds NaSeed CPIO limit: $path" >&2
                exit 1
            }
            printf '%s\n' "$path"
        done | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
printf '%s\n' "$packages" > "$work/requested-packages.txt"
du -h "$work/rootfs.cpio"
echo "[M38 ROOTFS] $port signed apk -> real GCC + binutils + musl CPIO"
