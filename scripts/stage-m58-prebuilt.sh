#!/bin/sh
# Host-only reuse of the existing signed Alpine v3.23 base rootfs builder.
# Deliberately does NOT touch rootfs/alpine/build.sh: M56/M57 expensive
# guest-compiler CI must not auto-trigger for M58 application-only changes.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in
  amd64) arch=x86_64; machine='Advanced Micro Devices X86-64' ;;
  riscv64) arch=riscv64; machine='RISC-V' ;;
  arm64) arch=aarch64; machine='AArch64' ;;
  *) exit 2 ;;
esac
port=$1
# M39 is the smallest existing signed Alpine application rootfs profile.
# Its signed target ISA keyring and APK database remain fully intact.
NAS_ALPINE_PROFILE=runtime sh rootfs/alpine/build.sh "$port"
base="build/external/m39-packages/$port"
work="build/external/m58-runtime/$port"
mkdir -p "$work"
cp -a "$base/rootfs" "$work/rootfs"
root="$work/rootfs"
target="$(pwd)/$root"
packages="$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' rootfs/alpine/packages-m58-runtime.txt)"

# Add three original precompiled Linux programs and ALL their runtime
# dependencies using Alpine's native signed APK solver on the HOST.
# Never run target-ISA install scripts or build anything in NaSeed.
if ! docker run --rm --volume "$target:/target" \
    --env APK_ARCH="$arch" --env APK_PACKAGES="$packages" \
    alpine:3.23 sh -ec '
        cp -a /target/etc/apk/keys/. /etc/apk/keys/
        apk --arch "$APK_ARCH" --root /target --keys-dir /etc/apk/keys \
            --no-scripts --quiet \
            -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
            add $APK_PACKAGES
    ' > "$work/apk-install.log" 2>&1; then
    echo "[M58 APK FAIL] $port" >&2
    tail -90 "$work/apk-install.log" >&2
    exit 1
fi
sudo chown -R "$(id -u):$(id -g)" "$root"

cp tests/m58-runtime-run.sh "$root/tests/run-m58.sh"
cp tests/m58-bash-suite.sh "$root/tests/m58-bash-suite.sh"
cp tests/m58-tcl-suite.tcl "$root/tests/m58-tcl-suite.tcl"
for program in bin/bash usr/bin/tclsh8.6 usr/bin/openssl; do
    binary="$root/$program"
    test -f "$binary" && test -x "$binary" || {
        echo "[M58 FAIL] missing original Alpine ELF: $program" >&2
        exit 1
    }
    readelf -h "$binary" | grep -Fq "$machine" || {
        echo "[M58 FAIL] wrong ISA: $program" >&2
        exit 1
    }
done
test -e "$root/usr/bin/tclsh"
# A compiler or source tree would change the nature of this milestone.
test ! -e "$root/tests/m58-source"
(
    cd "$root"
    find . \( -type f -o -type d -o -type l \) -print |
        LC_ALL=C sort |
        while IFS= read -r path; do
            [ "${#path}" -le 254 ] || {
                echo "[M58 FAIL] CPIO path too long: $path" >&2
                exit 1
            }
            printf '%s\n' "$path"
        done | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
printf '%s\n' "$packages" > "$work/requested-packages.txt"
echo "[M58 HOST STAGE] $port signed original Bash/Tcl/OpenSSL, zero guest compilation"
