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
# Alpine repository signing keys are ARCH-SPECIFIC. The amd64 container's
# keyring correctly verifies amd64 but rejects the arm64/riscv64 indexes.
# Obtain the official target release's keys, verify its minirootfs digest,
# extract ONLY the public keys, then continue with signed APK installation.
release=3.23.7
name="alpine-minirootfs-$release-$arch.tar.gz"
cache="build/deps/m38-alpine-release/$arch"
mkdir -p "$cache"
url="https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/$arch/$name"
if [ ! -s "$cache/$name" ]; then
    curl -fLsS --retry 2 --connect-timeout 10 --max-time 120 \
        -o "$cache/$name.tmp" "$url"
    mv "$cache/$name.tmp" "$cache/$name"
fi
curl -fLsS --retry 2 --connect-timeout 10 --max-time 30 \
    -o "$cache/$name.sha256" "$url.sha256"
( cd "$cache" && sha256sum -c "$name.sha256" )
# GNU tar --no-anchored permits both 'etc/...' and './etc/...' layouts.
tar -xzf "$cache/$name" -C "$root" \
    --wildcards --no-anchored 'etc/apk/keys/*'
test -n "$(find "$root/etc/apk/keys" -name '*.pub' -print -quit)"
# Use Alpine's own keyring. No --allow-untrusted; disable foreign package
# install scripts instead of executing incompatible target-ISA binaries.
docker run --rm --volume "$target:/target" \
    --env APK_ARCH="$arch" --env APK_PACKAGES="$packages" \
    alpine:3.23 sh -ec '
        # Target architecture's signed release keys were staged on host.
        test -n "$(find /target/etc/apk/keys -name "*.pub" -print -quit)"
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
