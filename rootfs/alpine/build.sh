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
    unix) work="build/external/m41-unix/$port"
          package_list=rootfs/alpine/packages-unix.txt ;;
    pthread) work="build/external/m42-pthread/$port"
             package_list=rootfs/alpine/packages-archive.txt ;;
    zstd) work="build/external/m43-zstd/$port"
          package_list=rootfs/alpine/packages-zstd.txt ;;
    group) work="build/external/m44-group/$port"
           package_list=rootfs/alpine/packages-zstd.txt ;;
    robust) work="build/external/m45-robust/$port"
            package_list=rootfs/alpine/packages-zstd.txt ;;
    exec) work="build/external/m46-exec/$port"
          package_list=rootfs/alpine/packages-zstd.txt ;;
    tgkill) work="build/external/m47-tgkill/$port"
            package_list=rootfs/alpine/packages-zstd.txt ;;
    parallel) work="build/external/m48-parallel/$port"
              package_list=rootfs/alpine/packages-parallel.txt ;;
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
elif [ "$profile" = parallel ]; then
    # Official GNU Make, GNU Findutils xargs -P and GNU GCC from signed
    # Alpine APKs. Host only stages source/Makefile. Compilation and
    # executable output are verified inside NaSeed's real QEMU guest.
    cp tests/m48-parallel-run.sh "$root/tests/run-parallel.sh"
    cp tests/m48-parallel.mk "$root/tests/m48-parallel.mk"
    cp tests/m48-parallel-worker.sh "$root/tests/m48-parallel-worker.sh"
    cp tests/m38-gcc-hello.c "$root/tests/m48-alpha.c"
    cp tests/m38-readv.c "$root/tests/m48-beta.c"
    printf '%s' 'abcdefghijklmn' > "$root/tests/readv.txt"
    for program in make gcc xargs; do
        valid=''
        for candidate in "$root/usr/bin/$program" "$root/bin/$program"; do
            if [ -x "$candidate" ] &&
               readelf -h "$candidate" 2>/dev/null | grep -Fq "$machine"; then
                valid=$candidate
                break
            fi
        done
        [ -n "$valid" ] || {
            echo "[M48] missing original distro $program ($port)" >&2
            exit 1
        }
    done
    test -s "$root/usr/include/stdio.h"
    test -s "$root/usr/lib/libc.a"
elif [ "$profile" = tgkill ]; then
    cp tests/m47-tgkill-run.sh "$root/tests/run-tgkill.sh"
    cp tests/m43-zstd-run.sh "$root/tests/run-zstd.sh"
    dd if=/dev/zero of="$root/tests/m43-input.bin" bs=1048576 count=12 status=none
    case "$port" in
        amd64) target_triple=x86_64-linux-musl ;;
        riscv64) target_triple=riscv64-linux-musl ;;
        arm64) target_triple=aarch64-linux-musl ;;
    esac
    clang --target="$target_triple" -O2 -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -nostdinc -fno-stack-protector -fno-builtin \
        -fno-pic -fno-pie -nostdlib -static -fuse-ld=lld \
        -Wl,-e,_start -Wl,-z,max-page-size=4096 \
        tests/m47-tgkill.c -o "$root/tests/m47-tgkill"
    readelf -h "$root/tests/m47-tgkill" | grep -Fq "$machine"
    test -x "$root/tests/m47-tgkill"
elif [ "$profile" = exec ]; then
    # Host builds the freestanding syscall corner-case pair for each ISA.
    # Authentic unmodified signed Alpine musl/zstd remains the regression.
    cp tests/m46-exec-run.sh "$root/tests/run-exec.sh"
    cp tests/m43-zstd-run.sh "$root/tests/run-zstd.sh"
    dd if=/dev/zero of="$root/tests/m43-input.bin" bs=1048576 count=12 status=none
    case "$port" in
        amd64) target_triple=x86_64-linux-musl ;;
        riscv64) target_triple=riscv64-linux-musl ;;
        arm64) target_triple=aarch64-linux-musl ;;
    esac
    for name in m46-exec m46-exec-target; do
        clang --target="$target_triple" -O2 -std=c11 -Wall -Wextra -Werror \
            -ffreestanding -nostdinc -fno-stack-protector -fno-builtin \
            -fno-pic -fno-pie -nostdlib -static -fuse-ld=lld \
            -Wl,-e,_start -Wl,-z,max-page-size=4096 \
            "tests/$name.c" -o "$root/tests/$name"
        readelf -h "$root/tests/$name" | grep -Fq "$machine"
        test -x "$root/tests/$name"
    done
elif [ "$profile" = robust ]; then
    # Host build: unchanged original native Alpine musl + zstd from
    # signed APK, plus a freestanding Linux syscall ABI conformance test.
    cp tests/m45-robust-run.sh "$root/tests/run-robust.sh"
    cp tests/m43-zstd-run.sh "$root/tests/run-zstd.sh"
    dd if=/dev/zero of="$root/tests/m43-input.bin" bs=1048576 count=12 status=none
    case "$port" in
        amd64) target_triple=x86_64-linux-musl ;;
        riscv64) target_triple=riscv64-linux-musl ;;
        arm64) target_triple=aarch64-linux-musl ;;
    esac
    clang --target="$target_triple" -O2 -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -nostdinc -fno-stack-protector -fno-builtin \
        -fno-pic -fno-pie -nostdlib -static -fuse-ld=lld \
        -Wl,-e,_start -Wl,-z,max-page-size=4096 \
        tests/m45-robust.c -o "$root/tests/m45-robust"
    readelf -h "$root/tests/m45-robust" | grep -Fq "$machine"
    test -x "$root/tests/m45-robust"
elif [ "$profile" = group ]; then
    # Test binary built ON THE HOST by upstream LLVM for target Linux64,
    # freestanding direct syscalls: no guest GCC, no fake test syscall.
    cp tests/m44-group-run.sh "$root/tests/run-group.sh"
    cp tests/m43-zstd-run.sh "$root/tests/run-zstd.sh"
    dd if=/dev/zero of="$root/tests/m43-input.bin" bs=1048576 count=12 status=none
    case "$port" in
        amd64) target_triple=x86_64-linux-musl ;;
        riscv64) target_triple=riscv64-linux-musl ;;
        arm64) target_triple=aarch64-linux-musl ;;
    esac
    clang --target="$target_triple" -O2 -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -nostdinc -fno-stack-protector -fno-builtin \
        -fno-pic -fno-pie -nostdlib -static -fuse-ld=lld \
        -Wl,-e,_start -Wl,-z,max-page-size=4096 \
        tests/m44-threadgroup.c -o "$root/tests/m44-threadgroup"
    readelf -h "$root/tests/m44-threadgroup" | grep -Fq "$machine"
    test -x "$root/tests/m44-threadgroup"
elif [ "$profile" = zstd ]; then
    # Original signed Alpine multi-threaded Zstandard: no guest compilation.
    # 12 MiB requires multiple compression input chunks at -T2; fixture
    # contains distinct blocks (not only a trivial all-zero test).
    dd if=/dev/zero of="$root/tests/m43-input.bin" bs=1048576 count=12 status=none
    printf 'NaSeed signed Alpine zstd pthread invariant M43\\n' |
        dd of="$root/tests/m43-input.bin" bs=1 conv=notrunc status=none
    cp tests/m43-zstd-run.sh "$root/tests/run-zstd.sh"
    original=''
    for candidate in "$root/usr/bin/zstd" "$root/bin/zstd"; do
        if [ -f "$candidate" ] && [ -x "$candidate" ] &&
           readelf -h "$candidate" | grep -Fq "$machine"; then
            original=$candidate
            break
        fi
    done
    [ -n "$original" ] || {
        echo "[M43] missing signed original zstd ELF ($port)" >&2
        exit 1
    }
elif [ "$profile" = pthread ]; then
    # Host stages original signed Alpine XZ and 4 MiB test input. All
    # actual multithreaded compression happens inside NaSeed's kernel.
    dd if=/dev/zero of="$root/tests/m42-input.bin" bs=1048576 count=4 status=none
    cp tests/m42-pthread-run.sh "$root/tests/run-pthread.sh"
    for program in xz; do
        for binary in "$root/usr/bin/$program" "$root/bin/$program"; do
            if [ -x "$binary" ] && readelf -h "$binary" | grep -Fq "$machine"; then
                break
            fi
        done
        test -x "$binary" || exit 1
    done
elif [ "$profile" = unix ]; then
    # One system acceptance: original ELF tools across fork/exec/pipe/tmpfs.
    mkdir -p "$root/tests/m41/raw/nested"
    cp tests/m41-alpha.txt "$root/tests/m41/raw/a.txt"
    cp tests/m41-beta.txt "$root/tests/m41/raw/nested/b.txt"
    cp tests/m41-expected.txt "$root/tests/m41/expected.txt"
    cp tests/m41-unix-run.sh "$root/tests/run-unix.sh"
    for program in find grep sed sort diff cp mv sha256sum; do
        binary=''
        for location in "$root/usr/bin/$program" "$root/bin/$program"; do
            if [ -x "$location" ] &&
               readelf -h "$location" 2>/dev/null | grep -Fq "$machine"; then
                # ELF is checked in situ: readlink -f follows absolute
                # *guest* symlinks in the host namespace and can silently
                # fail under set -e. The guest also validates GNU banners.
                binary=$location
                break
            fi
        done
        [ -n "$binary" ] || {
            echo "[M41] missing original native GNU $program ($port)" >&2
            exit 1
        }
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
# /bin/sh links are interpreted inside the GUEST rootfs, not host /.
# readlink -f on the host incorrectly resolves Alpine's absolute links
# (e.g. /usr/bin/busybox) outside the staged tree. Native QEMU must
# verify the real packaged shell, rather than a host-only path equality.
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
