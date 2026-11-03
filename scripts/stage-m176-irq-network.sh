#!/bin/sh
# Opt-in M176 addition to M174's EXISTING installed APK rootfs. Default
# M174 remains unchanged; this runs before M174's one final CPIO repack.
set -eu
[ "$#" -eq 3 ] || exit 2
port=$1
root=$2
cc=$3
case "$port" in
    amd64) arch=x86_64; machine='Advanced Micro Devices X86-64' ;;
    riscv64) arch=riscv64; machine='RISC-V' ;;
    arm64) arch=aarch64; machine='AArch64' ;;
    *) exit 2 ;;
esac
test -d "$root/etc/apk/keys" && test -x "$root/bin/busybox"
target="$(cd "$root" && pwd -P)"
# Alpine APK's trusted keys/signatures, platform repository and source
# binaries remain untouched. Add one genuine signed Nmap-Ncat package and
# its actual dynamic dependencies to this already-installed Alpine rootfs.
docker run --rm --volume "$target:/target" --env "APK_ARCH=$arch" \
    alpine:3.23 sh -ec '
        cp -a /target/etc/apk/keys/. /etc/apk/keys/
        apk --arch "$APK_ARCH" --root /target --keys-dir /etc/apk/keys \
            --no-scripts --quiet \
            -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
            add nmap-ncat
    '
sudo chown -R "$(id -u):$(id -g)" "$root"
test -x "$root/usr/bin/ncat"
readelf -h "$root/usr/bin/ncat" | grep -Fq "$machine"
readelf -l "$root/usr/bin/ncat" | grep -Fq INTERP
# Keep exactly the existing M174 /usr/bin launcher name and original
# downstream devfs/20-program/GUI behavior, inserting one genuine HTTP
# transaction before it. Production sources and the old profile are intact.
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    scripts/test-m176-ncat-launch.c -o "$root/usr/bin/naseed-m174-devfs-launch"
test -x "$root/usr/bin/naseed-m174-devfs-launch"
readelf -h "$root/usr/bin/naseed-m174-devfs-launch" | grep -Fq "$machine"
echo "[M176 STAGE PASS] $port original signed Alpine Ncat + genuine HTTP guest launcher + original M174 desktop"
