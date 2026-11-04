#!/bin/sh
# Add NaSeed's existing static desktop to ONE disposable, verified Alpine
# ext4 root. The original signed distribution programs remain untouched.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: stage-m198-alpine-desktop.sh PORT DISPOSABLE_ROOT' >&2; exit 2; }
port=$1 overlay=$2
case "$port" in
    amd64) cc=gcc; machine='Advanced Micro Devices X86-64' ;;
    riscv64) cc=riscv64-linux-gnu-gcc; machine='RISC-V' ;;
    arm64) cc=aarch64-linux-gnu-gcc; machine='AArch64' ;;
    *) exit 2 ;;
esac
[ -d "$overlay" ] && [ ! -L "$overlay" ] || exit 2
command -v "$cc" >/dev/null
work="build/external/m198-alpine-desktop/$port"
stage="$PWD/$work/native"
mkdir -p "$stage/usr/bin"
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
