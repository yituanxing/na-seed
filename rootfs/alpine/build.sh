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
    capacity) work="build/external/m49-capacity/$port"
              package_list=rootfs/alpine/packages-parallel.txt ;;
    git) work="build/external/m51-git/$port"
         package_list=rootfs/alpine/packages-git.txt ;;
    m85-git) work="build/external/m85-git/$port"
             package_list=rootfs/alpine/packages-git.txt ;;
    python) work="build/external/m52-python/$port"
            package_list=rootfs/alpine/packages-python.txt ;;
    vim) work="build/external/m53-vim/$port"
         package_list=rootfs/alpine/packages-vim.txt ;;
    dev) work="build/external/m54-dev/$port"
         package_list=rootfs/alpine/packages-dev.txt ;;
    cmake) work="build/external/m55-cjson/$port"
           package_list=rootfs/alpine/packages-cmake.txt ;;
    native-busybox) work="build/external/m56-native-busybox/$port"
           package_list=rootfs/alpine/packages-native-busybox.txt ;;
    sqlite) work="build/external/m57-native-sqlite/$port"
            package_list=rootfs/alpine/packages-native-busybox.txt ;;
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
# The *real Ninja* package (not a Samurai alias) lives in signed Alpine
# community. Only the CMake workload needs this additional official index.
extra_repository=''
if [ "$profile" = cmake ]; then
    extra_repository='-X https://dl-cdn.alpinelinux.org/alpine/v3.23/community'
fi
# Use Alpine's own keyring. No --allow-untrusted; disable foreign package
# install scripts instead of executing incompatible target-ISA binaries.
# Bound CI output; diagnostics are always saved and the last lines
# surfaced on failure. Large package extraction can print thousands of lines.
if ! docker run --rm --volume "$target:/target" \
    --env APK_ARCH="$arch" --env APK_PACKAGES="$packages" \
    --env APK_EXTRA_REPOSITORY="$extra_repository" \
    alpine:3.23 sh -ec '
        test -n "$(find /target/etc/apk/keys -name "*.pub" -print -quit)"
        # apk2/apk3 differ in how --keys-dir interacts with --root.
        # Seed BOTH views with the signed release cross-ISA keys,
        # keeping signature verification strictly enabled.
        cp -a /target/etc/apk/keys/. /etc/apk/keys/
        apk --arch "$APK_ARCH" --root /target \
            --keys-dir /etc/apk/keys --initdb --no-scripts --quiet \
            -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
            $APK_EXTRA_REPOSITORY add $APK_PACKAGES
    ' > "$work/apk-install.log" 2>&1; then
    echo "[M38 APK FAIL] $port" >&2
    tail -90 "$work/apk-install.log" >&2
    exit 1
fi
echo "[M38 APK PASS] signed distro packages installed: $port"
sudo chown -R "$(id -u):$(id -g)" "$root"
mkdir -p "$root/tmp" "$root/tests"
# Common, read-only guest timing and CPU occupancy helpers for original
# source-build acceptance. They sample genuine /proc/uptime and /proc/stat,
# never patch GCC, Kbuild, CMake or Ninja or replace acceptance markers.
cp tests/native-perf-clock.sh "$root/tests/native-perf-clock.sh"
cp scripts/native-profile-guest.sh "$root/tests/native-profile-guest.sh"
chmod 1777 "$root/tmp"
test -e "$root/bin/busybox"
if [ "$profile" = native-busybox ]; then
    # Stage ONLY a pinned, ORIGINAL upstream source tree. No host-produced
    # BusyBox binary, generated .config, Makefile rewriting or host build.
    # All Kconfig HOSTCC, target GCC, GNU Make, link and execution occur
    # under the actual NaSeed kernel in QEMU.
    version=1.37.0
    expected_sha=3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4
    upstream="build/deps/source-busybox/busybox-$version.tar.bz2"
    mkdir -p "$(dirname "$upstream")"
    if [ ! -s "$upstream" ]; then
        # The publisher occasionally times out in GitHub-hosted CI.
        # These are byte-for-byte original upstream release mirrors, NOT
        # re-packed git archives. Verify the SAME pinned official SHA256
        # separately for every attempted download, never allow untrusted
        # or changed BusyBox source into the NaSeed guest.
        for url in \
            "https://sources.buildroot.net/busybox/busybox-$version.tar.bz2" \
            "https://mirrors.nju.edu.cn/buildroot/busybox/busybox-$version.tar.bz2" \
            "https://busybox.net/downloads/busybox-$version.tar.bz2"; do
            rm -f "$upstream.tmp"
            if curl -fLsS --retry 1 --retry-delay 1 \
                    --connect-timeout 8 --max-time 45 \
                    -o "$upstream.tmp" "$url" &&
               printf '%s  %s\n' "$expected_sha" "$upstream.tmp" |
                    sha256sum -c -; then
                mv "$upstream.tmp" "$upstream"
                echo "[M56 SOURCE] original SHA256 verified via $url"
                break
            fi
        done
        test -s "$upstream" || { echo '[M56 SOURCE FAIL] no trusted pinned archive reachable' >&2; exit 1; }
    fi
    printf '%s  %s\n' "$expected_sha" "$upstream" | sha256sum -c -
    mkdir -p "$root/tests/m56-source"
    tar -xjf "$upstream" -C "$root/tests/m56-source" --strip-components=1
    test -s "$root/tests/m56-source/Makefile"
    test -s "$root/tests/m56-source/Config.in"
    # No generated executable or preconfigured project is permitted.
    test ! -e "$root/tests/m56-source/busybox"
    test ! -e "$root/tests/m56-source/.config"
    cp scripts/source-busybox.config "$root/tests/m56.config"
    cp tests/m56-busybox-run.sh "$root/tests/run-m56.sh"
    for program in sed awk find xargs bzip2 diff cmp gzip grep; do
        # Signed Alpine packages install their original utilities in /bin
        # OR /usr/bin (e.g. GNU sed is /bin/sed). Never conflate a host
        # filesystem path with the guest's standard PATH.
        found=''
        for candidate in "$root/usr/bin/$program" "$root/bin/$program"; do
            if [ -x "$candidate" ]; then found=$candidate; break; fi
        done
        [ -n "$found" ] || {
            echo "[M56 FAIL] original Alpine tool missing: $program" >&2
            exit 1
        }
        readelf -h "$found" | grep -Fq "$machine" || {
            echo "[M56 FAIL] wrong-ISA tool: $program" >&2
            exit 1
        }
    done
    for program in make gcc as ld readelf; do
        test -x "$root/usr/bin/$program" || {
            echo "[M56 FAIL] missing native tool: $program" >&2
            exit 1
        }
        readelf -h "$root/usr/bin/$program" | grep -Fq "$machine"
    done
    test -s "$root/usr/lib/libc.a"
    test -s "$root/usr/include/stdio.h"
    echo "[M56 SOURCE PASS] original SHA256-pinned BusyBox $version + native GNU toolchain staged for genuine guest build"
elif [ "$profile" = sqlite ]; then
    # Official SQLite 3.54.1 AUTOCONF release, not the Alpine sqlite package.
    # Reuse the SHA3-256 source owner also used by M84; no second fetcher.
    # Native ./configure, Makefile, GCC and resulting SQLite CLI run INSIDE
    # the NaSeed guest. Host extraction only populates immutable bootfs.
    archive="$(sh scripts/fetch-m57-sqlite.sh)"
    src="$root/tests/m57-source"
    mkdir -p "$src"
    tar -xzf "$archive" -C "$src" --strip-components=1
    for file in configure Makefile.in sqlite3.c sqlite3.h shell.c; do
        test -s "$src/$file" || {
            echo "[M57 FAIL] upstream SQLite source missing $file" >&2
            exit 1
        }
    done
    test ! -e "$src/sqlite3"
    cp tests/m57-sqlite-run.sh "$root/tests/run-sqlite.sh"
    cp tests/m57-largefile.c "$root/tests/m57-largefile.c"
    for program in gcc make as ld readelf; do
        test -x "$root/usr/bin/$program"
        readelf -h "$root/usr/bin/$program" | grep -Fq "$machine"
    done
    for program in sed awk grep; do
        candidate=''
        for location in "$root/bin/$program" "$root/usr/bin/$program"; do
            if [ -x "$location" ] &&
               readelf -h "$location" | grep -Fq "$machine"; then
                candidate=$location
                break
            fi
        done
        test -n "$candidate" || {
            echo "[M57 FAIL] missing native SQLite configure tool: $program" >&2
            exit 1
        }
    done
    test -s "$root/usr/lib/libc.a"
    test -s "$root/usr/include/stdio.h"
    echo "[M57 SOURCE PASS] official SQLite 3.54.1 SHA3-256 source, native GNU toolchain"
elif [ "$profile" = cmake ]; then
    # Pinned genuine upstream cJSON 1.7.19 commit. The bytes of the CMake
    # build scripts and code are verified against Git object hashes; no
    # host-generated project or altered upstream source.
    upstream_sha=c859b25da02955fef659d658b8f324b5cde87be3
    archive="build/deps/m55-cjson-$upstream_sha.tar.gz"
    mkdir -p build/deps
    if [ ! -s "$archive" ]; then
        curl -fLsS --retry 3 --connect-timeout 10 --max-time 120 \
            -o "$archive.tmp" \
            "https://codeload.github.com/DaveGamble/cJSON/tar.gz/$upstream_sha"
        mv "$archive.tmp" "$archive"
    fi
    src="$root/tests/m55-cjson"
    mkdir -p "$src"
    tar -xzf "$archive" -C "$src" --strip-components=1
    for entry in \
        'CMakeLists.txt:c7ca27f09e628e2d786eb344ff4d04004d6c0d38' \
        'cJSON.c:6e4fb0dd369cd905923da515be87ab06db6c1ee0' \
        'cJSON.h:cab5feb427725f8e5c82287f7fe59481b609b9b5' \
        'test.c:986fc6eb3e77fc29cca2980647c357bf1fe1b1fe' \
        'tests/CMakeLists.txt:c7592213b91236600136e670c7be4d5834a6fbe1' \
        'tests/unity/src/unity.c:eee6f965e72ce28f0be9a91ad5331ee25fe2c2f0'; do
        relative=${entry%%:*}
        expected=${entry#*:}
        test "$(git hash-object "$src/$relative")" = "$expected" || {
            echo "[M55 FAIL] upstream cJSON blob mismatch: $relative" >&2
            exit 1
        }
    done
    cp tests/m55-cjson-run.sh "$root/tests/run-cjson.sh"
    cp tests/m55-epoll.c "$root/tests/m55-epoll.c"
    cp tests/m55-eventfd.c "$root/tests/m55-eventfd.c"
    cp tests/m55-stack.c "$root/tests/m55-stack.c"
    for program in gcc cmake ctest ninja; do
        test -x "$root/usr/bin/$program" || {
            echo "[M55 FAIL] target native executable missing $program" >&2
            exit 1
        }
    done
    for program in gcc cmake ctest; do
        readelf -h "$root/usr/bin/$program" | grep -Fq "$machine"
    done
    # Alpine Ninja's command is a packaged symlink into ninja-build/bin;
    # validate its actual ELF directly (host readelf cannot resolve an
    # absolute symlink in the guest rootfs).
    readelf -h "$root/usr/lib/ninja-build/bin/ninja" | grep -Fq "$machine"
    test -s "$root/usr/lib/libc.a"
elif [ "$profile" = dev ]; then
    # One rootfs with ORIGINAL native Alpine GNU GCC/Make, Vim, Git,
    # CPython and BusyBox. Entire project lifecycle happens in guest.
    cp tests/m54-dev-run.sh "$root/tests/run-dev.sh"
    cp tests/m54-dev-check.py "$root/tests/dev-check.py"
    mkdir -p "$root/tests/m54-src"
    cp tests/m54-src/answer.c tests/m54-src/answer.h \
       tests/m54-src/main.c tests/m54-src/Makefile \
       tests/m54-src/.gitignore "$root/tests/m54-src/"
    for program in gcc make git vim python3; do
        candidate="$root/usr/bin/$program"
        test -x "$candidate" || { echo "[M54] missing signed $program ($port)" >&2; exit 1; }
        readelf -h "$candidate" | grep -Fq "$machine"
    done
    test -s "$root/usr/include/stdio.h"
    test -s "$root/usr/lib/libc.a"
    test -s "$root/usr/lib/python3.12/json/__init__.py"
    if ! test -e "$root/etc/terminfo/v/vt100" && \
       ! test -e "$root/usr/share/terminfo/v/vt100"; then
        echo "[M54] original vt100 terminfo missing" >&2; exit 1
    fi
elif [ "$profile" = vim ]; then
    # Unmodified signed Alpine Vim and ncurses data. The launcher is a
    # generic Linux C user program, never a kernel test-specific syscall.
    cp tests/m53-vim-run.sh "$root/tests/m53-vim-run.sh"
    test -x "$root/usr/bin/vim" || { echo "[M53] missing native Vim" >&2; exit 1; }
    readelf -h "$root/usr/bin/vim" | grep -Fq "$machine"
    test -s "$root/usr/include/stdio.h"
    test -s "$root/usr/lib/libc.a"
    # Compile against target Alpine musl with the same host LLVM on each ISA.
    # No glibc cross-system is borrowed from a different Linux distro.
    case "$port" in
        amd64) triple=x86_64-linux-musl ;;
        riscv64) triple=riscv64-linux-musl ;;
        arm64) triple=aarch64-linux-musl ;;
    esac
    # The Clang driver would otherwise seek an unrelated host GCC runtime
    # (crtbeginT/libgcc). Link using the *signed target musl* crt1 + libc
    # directly, exactly the same architecture as the Alpine Vim runtime.
    test -s "$root/usr/lib/crt1.o"
    clang --target="$triple" --sysroot="$root" -O2 -std=c11 -Wall -Wextra \
        -Werror -static -fno-pie -nostdlib -fuse-ld=lld \
        -Wl,-z,max-page-size=4096 \
        "$root/usr/lib/crt1.o" tests/m53-vim-init.c \
        "$root/usr/lib/libc.a" -o "$root/tests/m53-vim-init"
    readelf -h "$root/tests/m53-vim-init" | grep -Fq "$machine"
    if readelf -l "$root/tests/m53-vim-init" | grep -q INTERP; then
        echo "[M53] launcher must be genuine native static Linux ELF" >&2
        exit 1
    fi
    if ! test -e "$root/etc/terminfo/v/vt100" && \
       ! test -e "$root/usr/share/terminfo/v/vt100"; then
        echo "[M53] signed vt100 terminfo missing" >&2
        exit 1
    fi
elif [ "$profile" = python ]; then
    # Original native signed CPython ELF and complete upstream stdlib.
    cp tests/m52-python-run.sh "$root/tests/run-python.sh"
    cp tests/m52-python-suite.py "$root/tests/m52-python-suite.py"
    test -x "$root/usr/bin/python3"
    readelf -h "$root/usr/bin/python3" | grep -Fq "$machine"
    test -s "$root/usr/lib/python3.12/os.py"
    test -s "$root/usr/lib/python3.12/encodings/__init__.py"
    test -s "$root/usr/lib/python3.12/encodings/utf_8.py"
elif [ "$profile" = git ]; then
    # Original signed native Alpine Git and its installed library closure.
    cp tests/m51-git-run.sh "$root/tests/run-git.sh"
    test -x "$root/usr/bin/git" || { echo "[M51] missing Git ELF ($port)" >&2; exit 1; }
    readelf -h "$root/usr/bin/git" | grep -Fq "$machine"
elif [ "$profile" = m85-git ]; then
    # Same signed Git APK closure as M51; only the REAL guest workload differs.
    # The runner adds the independently prepared VirtIO ext4 disk at /mnt.
    cp tests/m85-git-run.sh "$root/tests/m85-git-run.sh"
    test -x "$root/usr/bin/git" || { echo "[M85] missing signed Git ELF ($port)" >&2; exit 1; }
    readelf -h "$root/usr/bin/git" | grep -Fq "$machine"
elif [ "$profile" = capacity ]; then
    # M49 runs only real packaged GNU Make/GCC/xargs and guest-built musl
    # binaries; no new syscall stubs, no precompiled fake guest executables.
    cp tests/m49-capacity-run.sh "$root/tests/run-capacity.sh"
    cp tests/m49-parallel.mk "$root/tests/m49-parallel.mk"
    cp tests/m49-xargs-worker.sh "$root/tests/m49-xargs-worker.sh"
    cp tests/m49-fork-capacity.c "$root/tests/m49-fork-capacity.c"
    for suffix in alpha beta gamma delta; do
        case "$suffix" in
          alpha|gamma) cp tests/m38-gcc-hello.c "$root/tests/m49-$suffix.c" ;;
          beta|delta) cp tests/m38-readv.c "$root/tests/m49-$suffix.c" ;;
        esac
    done
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
            echo "[M49] missing signed native GNU $program ($port)" >&2
            exit 1
        }
    done
    test -s "$root/usr/include/stdio.h"
    test -s "$root/usr/lib/libc.a"
elif [ "$profile" = archive ]; then
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
    # Same upstream-LLVM source, independently started as PID 1 with
    # leader-first SYS_exit (NOT worker exit_group or a test-mode syscall).
    clang --target="$target_triple" -O2 -std=c11 -Wall -Wextra -Werror \
        -DM44_INIT_LEADER_FIRST \
        -ffreestanding -nostdinc -fno-stack-protector -fno-builtin \
        -fno-pic -fno-pie -nostdlib -static -fuse-ld=lld \
        -Wl,-e,_start -Wl,-z,max-page-size=4096 \
        tests/m44-threadgroup.c -o "$root/tests/m44-pid1-leader"
    readelf -h "$root/tests/m44-pid1-leader" | grep -Fq "$machine"
    test -x "$root/tests/m44-pid1-leader"
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
