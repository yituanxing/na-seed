#!/bin/sh
# Real signed fbgrab/magick + explicit separately identified C acceptance app.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m164-batch
work="build/external/m164-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio"
test -s "$root/tests/m164-input-display"
test -s "$root/tests/m164-input-display.c"
for package in fbgrab imagemagick; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed"
done
canonical=$(cd "$root" && pwd -P)
for program in fbgrab magick; do
    resolved=$(readlink -f "$root/usr/bin/$program") || exit 1
    case "$resolved" in "$canonical"/*) ;; *) exit 1 ;; esac
    test -x "$resolved" || exit 1
    test "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46
    sha256sum "$resolved"
done > "$work/m164-original-signed-elfs.sha256"
[ "$(awk '{print $1}' "$work/m164-original-signed-elfs.sha256" | sort -u | wc -l)" -eq 2 ]
echo "[M164 SIGNED STAGE PASS] $port original Alpine framebuffer tools + separate native C app"
