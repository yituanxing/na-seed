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
profile=${NAS_ALPINE_PROFILE:-toolchain}
case "$profile" in
    toolchain) work="build/external/m38-gcc/$port"
               package_list=rootfs/alpine/packages.txt ;;
    runtime) work="build/external/m39-packages/$port"
             package_list=rootfs/alpine/packages-runtime.txt ;;
    archive) work="build/external/m40-archive/$port"
             package_list=rootfs/alpine/packages-archive.txt ;;
    *) echo "[ALPINE] unknown profile $profile" >&2; exit 2 ;;
esac
root="$work/rootfs"
mkdir -p "$work"
# Staged trees may contain root-owned APK metadata from Docker.
if [ -e "$root" ]; then sudo rm -rf "$root"; fi
mkdir -p "$root"
command -v docker >/dev/null
command -v cpio >/dev/null
command -v readelf >/dev/null
target="$(pwd)/$root"
packages="$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$package_list")"
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
# Bound CI output; diagnostics are always saved and the last lines
# surfaced on failure. Large package extraction can print thousands of lines.
if ! docker run --rm --volume "$target:/target" \
    --env APK_ARCH="$arch" --env APK_PACKAGES="$packages" \
    alpine:3.23 sh -ec '
        test -n "$(find /target/etc/apk/keys -name "*.pub" -print -quit)"
        # apk2/apk3 differ in how --keys-dir interacts with --root.
        # Seed BOTH views with the signed release cross-ISA keys,
        # keeping signature verification strictly enabled.
        cp -a /target/etc/apk/keys/. /etc/apk/keys/
        apk --arch "$APK_ARCH" --root /target \
            --keys-dir /etc/apk/keys --initdb --no-scripts --quiet \
            -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
            add $APK_PACKAGES
    ' > "$work/apk-install.log" 2>&1; then
    echo "[M38 APK FAIL] $port" >&2
    tail -90 "$work/apk-install.log" >&2
    exit 1
fi
echo "[M38 APK PASS] signed distro packages installed: $port"
sudo chown -R "$(id -u):$(id -g)" "$root"
mkdir -p "$root/tmp" "$root/tests"
chmod 1777 "$root/tmp"
test -e "$root/bin/busybox"
if [ "$profile" = archive ]; then
    # Original prebuilt GNU tar and XZ Utils — NEVER built inside NaSeed.
    # Stage a small nested CPIO fixture; all archive creation/extraction,
    # compression, decompression and verification happen in the guest.
    mkdir -p "$root/tests/archive-seed/nested"
    cp tests/m40-seed-alpha.txt "$root/tests/archive-seed/alpha.txt"
    cp tests/m40-seed-beta.txt "$root/tests/archive-seed/nested/beta.txt"
    cp tests/m40-archives-run.sh "$root/tests/run-archives.sh"
    for program in tar xz; do
        binary=''
        for location in "$root/usr/bin/$program" "$root/bin/$program"; do
            if [ -f "$location" ] && [ -x "$location" ] &&
               [ ! -L "$location" ]; then
                binary=$location
                break
            fi
        done
        [ -n "$binary" ] || { echo "[M40] missing original packaged $program ($port)" >&2; exit 1; }
        readelf -h "$binary" | grep -Fq "$machine"
    done
elif [ "$profile" = runtime ]; then
    # These are original Alpine executables, *not* compiled in NaSeed.
    cp tests/lua-vm.lua tests/lua-io.lua "$root/tests/"
    cp tests/m39-packages-run.sh "$root/tests/run-packages.sh"
    cp tests/m39-jobs.json "$root/tests/jobs.json"
    for program in lua5.4 jq sqlite3; do
        test -x "$root/usr/bin/$program" || {
            echo "[M39] missing original Alpine program: $program ($port)" >&2
            exit 1
        }
        readelf -h "$root/usr/bin/$program" | grep -Fq "$machine"
    done
else
    cp tests/m38-gcc-hello.c "$root/tests/hello.c"
    cp tests/m38-gcc-build.sh "$root/tests/build.sh"
    cp tests/m38-readv.c "$root/tests/readv.c"
    cp tests/m38-fs-mode.c "$root/tests/fs-mode.c"
    printf '%s' 'abcdefghijklmn' > "$root/tests/readv.txt"
    test -s "$root/usr/include/stdio.h"
    test -s "$root/usr/include/stdlib.h"
    test -s "$root/usr/lib/libc.a"
    test -e "$root/usr/bin/gcc"
    test -e "$root/usr/bin/as"
    test -e "$root/usr/bin/ld"
    readelf -h "$root/usr/bin/gcc" | grep -Fq "$machine"
fi
# apk --no-scripts is required for cross-ISA installs; Alpine's BusyBox
# post-install trigger ordinarily creates /bin/sh. Materialize that standard
# package link without executing an incompatible-architecture install hook.
if [ ! -e "$root/bin/sh" ]; then
    [ ! -L "$root/bin/sh" ] || { echo '[M38] broken distro /bin/sh link' >&2; exit 1; }
    ln -s busybox "$root/bin/sh"
fi
test -x "$root/bin/sh"
test "$(readlink -f "$root/bin/sh")" = "$(readlink -f "$root/bin/busybox")"
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
echo "[ALPINE ROOTFS] $port profile=$profile signed APK binaries, original ELF and shared musl CPIO"
