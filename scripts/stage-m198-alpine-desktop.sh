#!/bin/sh
# Add NaSeed's existing static desktop to ONE disposable, verified Alpine
# ext4 root. The original signed distribution programs remain untouched.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: stage-m198-alpine-desktop.sh PORT DISPOSABLE_ROOT' >&2; exit 2; }
port=$1 overlay=$2
case "$port" in
    amd64) cc=gcc; machine='Advanced Micro Devices X86-64'; apk_arch=x86_64 ;;
    riscv64) cc=riscv64-linux-gnu-gcc; machine='RISC-V'; apk_arch=riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc; machine='AArch64'; apk_arch=aarch64 ;;
    *) exit 2 ;;
esac
[ -d "$overlay" ] && [ ! -L "$overlay" ] || exit 2
case "$overlay" in /*) ;; *) overlay="$PWD/$overlay" ;; esac
command -v "$cc" >/dev/null
command -v docker >/dev/null
work="build/external/m198-alpine-desktop/$port"
stage="$PWD/$work/native"
mkdir -p "$stage/usr/bin"
# Reuse the existing M191/Alpine-builder mechanism for a signed, foreign-ISA
# APK transaction on the disposable host overlay. M197 still performs its
# public HTTPS/signed Bash/Lua/SQLite/Git transaction INSIDE NaSeed/QEMU;
# moving only extra editor installation here saves a second >10-minute WAN
# APK extraction under emulation. Preserve the overlay's existing apk DB:
# unlike rootfs/alpine/build.sh's initial root creation, do NOT --initdb.
if ! docker run --rm --volume "$overlay:/target" \
        --env APK_ARCH="$apk_arch" alpine:3.23 sh -ec '
        test -n "$(find /target/etc/apk/keys -name "*.pub" -print -quit)"
        cp -a /target/etc/apk/keys/. /etc/apk/keys/
        apk --arch "$APK_ARCH" --root /target --keys-dir /etc/apk/keys \
            --no-scripts --quiet \
            -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
            add vim nano ncurses-terminfo-base
    ' > "$PWD/$work/m198-signed-apk-stage.log" 2>&1; then
    echo "[M198 APK STAGE FAIL] $port signed original Alpine editors" >&2
    tail -90 "$PWD/$work/m198-signed-apk-stage.log" >&2
    exit 1
fi
for app in vim nano; do
    test -x "$overlay/usr/bin/$app"
    readelf -h "$overlay/usr/bin/$app" | grep -Fq "$machine"
done
echo "[M198 APK STAGE PASS] $port official signed original Alpine Vim/Nano"
for app in naseed-image naseed-terminal naseed-browser; do
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        "user/desktop/$app.c" user/desktop/protocol.c user/desktop/text.c \
        -o "$stage/usr/bin/$app"
done
# Exactly the existing M171 upstream LVGL and M167/M173 compositor build.
sh scripts/host-configure-m171-lvgl.sh "$port" "$stage"
for app in naseed-desktop naseed-image naseed-terminal naseed-browser naseed-lvgl; do
    file="$stage/usr/bin/$app"
    test -x "$file"
    readelf -h "$file" | grep -Fq "$machine"
    if readelf -l "$file" | grep -Fq INTERP; then
        echo "[M198 STAGE FAIL] $app is not a static NaSeed client" >&2
        exit 1
    fi
    sudo install -m 0755 "$file" "$overlay/usr/bin/$app"
done
sudo install -d -m 0755 "$overlay/tests"
sudo install -m 0644 tests/m167-desktop-fixture.ppm \
    "$overlay/tests/m167-desktop-fixture.ppm"
sudo install -m 0644 tests/m168-home.html "$overlay/tests/m168-home.html"
sudo test ! -e "$overlay/root/m198-editor.txt" || {
    echo '[M198 STAGE FAIL] persistent GUI editor file was preseeded' >&2
    exit 1
}
echo "[M198 STAGE PASS] $port original Alpine PID1 kept; five independent "\
    'static NaSeed GUI clients and two data fixtures added to disposable ext4'
