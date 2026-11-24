#!/bin/sh
# Stage four NEW signed Alpine ELF programs alongside the genuine evdev test
# executable.  The test app is compiled separately and is never counted as
# an original package binary.  No guest compilation or APK scripts execute.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m166-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M166 "$manifest" --count --distinct)
[ "$count" -eq 4 ] || exit 1
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m166-batch
work="build/external/m166-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -x "$root/tests/m166-input-display"
test -s "$root/tests/m166-input-display.c"
for package in evtest libevdev-tools fbgrab imagemagick; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed"
done
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m166-original-signed-elfs.sha256" 4 contained-exec-elf M166
echo "[M166 SIGNED STAGE PASS] $port four new independent original Alpine evdev ELFs"
