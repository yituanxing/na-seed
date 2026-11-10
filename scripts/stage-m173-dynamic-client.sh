#!/bin/sh
# Keep the signed Alpine and pinned LVGL bytes from M171.  An ordinary
# interactive BusyBox shell can now launch a SECOND instance without the
# compositor knowing the fifth executable's path or holding its PID.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/stage-m171-lvgl.sh "$port"
work="build/external/m168-batch/$port"
root="$work/rootfs"
test -x "$root/usr/bin/naseed-lvgl"
ln -sfn naseed-lvgl "$root/usr/bin/gui"
test -L "$root/usr/bin/gui"
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
echo "[M173 STAGE PASS] $port same pinned LVGL ELF, separately shell-executable /usr/bin/gui alias"
