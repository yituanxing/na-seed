#!/bin/sh
# Restore historical BusyBox native ABI acceptance after Alpine rotates APKs:
# exact v3.23 musl runtime revision selected from a SIGNATURE-VERIFIED APK
# index and target-ISA Alpine release keys, never a raw mirror or fake loader.
# Usage: install-verified-alpine-musl.sh x86_64|aarch64|riscv64 TARGET_ROOT
set -eu
[ "$#" -eq 2 ] || exit 2
arch=$1 dest=$2
case "$arch" in x86_64|aarch64|riscv64) ;; *) exit 2 ;; esac
command -v docker >/dev/null
command -v readelf >/dev/null
# This exact verified musl APK revision currently exists on Alpine v3.23.
# Change it only with the Alpine package security update and CI oracle.
version=1.2.5-r24
release=3.23.7
name="alpine-minirootfs-$release-$arch.tar.gz"
cache="build/deps/m38-alpine-release/$arch"
mkdir -p "$cache" "$dest"
url="https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/$arch/$name"
# Minirootfs sha256 is release-authenticated HTTPS; its public signing keys
# are additionally used for APK index/package signature verification.
curl -fLsS --retry 2 --connect-timeout 10 --max-time 45 \
    -o "$cache/$name.sha256" "$url.sha256"
if [ ! -s "$cache/$name" ] || ! (
    cd "$cache"
    sha256sum -c "$name.sha256" >/dev/null 2>&1
); then
    curl -fLsS --retry 2 --connect-timeout 10 --max-time 120 \
        -o "$cache/$name.tmp" "$url"
    mv "$cache/$name.tmp" "$cache/$name"
fi
( cd "$cache" && sha256sum -c "$name.sha256" )
tar -xzf "$cache/$name" -C "$dest" \
    --wildcards --no-anchored 'etc/apk/keys/*'
test -n "$(find "$dest/etc/apk/keys" -name '*.pub' -print -quit)"
absolute="$(cd "$dest" && pwd)"
# Matches rootfs/alpine/build.sh's already proven three-ISA verified apk
# transport. Scripts from foreign binaries never execute on the host.
docker run --rm --volume "$absolute:/target" \
    --env APK_ARCH="$arch" --env MUSL_VERSION="$version" \
    alpine:3.23 sh -ec '
        cp -a /target/etc/apk/keys/. /etc/apk/keys/
        apk --arch "$APK_ARCH" --root /target \
            --keys-dir /etc/apk/keys --initdb --no-scripts --quiet \
            -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
            add "musl=$MUSL_VERSION"
    '
loader="$dest/lib/ld-musl-$arch.so.1"
test -e "$loader"
case "$arch" in
  x86_64) machine='Advanced Micro Devices X86-64' ;;
  riscv64) machine='RISC-V' ;;
  aarch64) machine='AArch64' ;;
esac
readelf -h "$loader" | grep -Fq "$machine"
sha256sum "$loader"
printf '[ALPINE SIGNED] %s musl=%s original APK ELF verified\n' "$arch" "$version"
