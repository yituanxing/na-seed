#!/bin/sh
# Add an ordinary desktop service to a DISPOSABLE, original Alpine ext4 tree.
# The existing M191/M198 signed distribution baseline is never modified.
set -eu
[ "$#" -eq 2 ] || {
    echo 'usage: stage-m199-alpine-openrc.sh PORT DISPOSABLE_ALPINE_ROOT' >&2
    exit 2
}
port=$1
overlay=$2
case "$port" in
    amd64|riscv64|arm64) ;;
    *) echo "[M199 STAGE FAIL] unsupported port: $port" >&2; exit 2 ;;
esac
case "$overlay" in /*) ;; *) overlay="$PWD/$overlay" ;; esac
[ -d "$overlay" ] && [ ! -L "$overlay" ] || {
    echo '[M199 STAGE FAIL] missing or symlinked disposable Alpine root' >&2
    exit 2
}
test -f "$overlay/etc/alpine-release" &&
test -s "$overlay/etc/inittab" &&
test -x "$overlay/usr/bin/naseed-desktop" || {
    echo '[M199 STAGE FAIL] original Alpine+M198 desktop prerequisite absent' >&2
    exit 1
}
test -e "$overlay/sbin/openrc-run" || test -L "$overlay/sbin/openrc-run" || {
    echo '[M199 STAGE FAIL] missing original Alpine OpenRC script runner' >&2
    exit 1
}

service="$overlay/etc/init.d/naseed-desktop"
enabled="$overlay/etc/runlevels/default/naseed-desktop"
if [ -e "$service" ] || [ -L "$service" ] ||
   [ -e "$enabled" ] || [ -L "$enabled" ]; then
    echo '[M199 STAGE FAIL] refusing to overwrite a pre-existing service' >&2
    exit 1
fi

# Stage just the third-party service. The first real Alpine boot must use its
# own original `rc-update add naseed-desktop default` to enable it; the NEXT
# QEMU boot is then the independent OpenRC auto-start proof. Do not preseed a
# runlevel symlink on the host, or this acceptance could prove nothing.
sudo install -d -m 0755 "$overlay/etc/init.d"
sudo install -m 0755 rootfs/alpine/naseed-desktop.openrc "$service"
sudo test -x "$service"
test ! -e "$enabled" && test ! -L "$enabled"
echo "[M199 STAGE PASS] $port original Alpine OpenRC service staged; "\
     'default runlevel intentionally absent until real guest rc-update'
