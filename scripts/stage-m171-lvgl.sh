#!/bin/sh
# Reuse signed APK overlay; only NaSeed's GUI/compositor are added.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
sh scripts/stage-m168-prebuilt.sh "$port"
work="build/external/m168-batch/$port"
root="$work/rootfs"
sh scripts/host-configure-m171-lvgl.sh "$port" "$root"
(
    cd "$root"
    find . \( -type f -o -type d -o -type l \) -print |
        LC_ALL=C sort |
        while IFS= read -r path; do
            [ "$(printf %s "$path" | wc -c)" -le 254 ] || exit 1
            printf '%s\n' "$path"
        done | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo "[M171 STAGE PASS] $port original Alpine binaries + independent LVGL GUI ELF"
