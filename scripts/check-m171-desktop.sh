#!/bin/sh
# M171 host proof uses the actual pinned LVGL library, not fake rasterization.
set -eu
sh scripts/check-m170-desktop.sh
work=$(mktemp -d /tmp/naseed-m171.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/root/usr/bin"
for app in naseed-image naseed-terminal naseed-browser; do
    gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie       "user/desktop/$app.c" user/desktop/protocol.c user/desktop/text.c       -o "$work/root/usr/bin/$app"
done
# Reuse the production cross-build owner with the host x86-64 compiler.
sh scripts/host-configure-m171-lvgl.sh amd64 "$work/root"
test -s "$work/root/usr/bin/naseed-lvgl"
test -s build/external/m171-lvgl/amd64/host-lvgl.log
grep -Fq '[M171 HOST LVGL PASS]' build/external/m171-lvgl/amd64/host-lvgl.log
echo '[M171 HOST PASS] actual upstream LVGL 9.4, memfd flush and pointer event'
