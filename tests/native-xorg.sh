#!/bin/sh
# X11 compatibility is a separate native QEMU acceptance image.
# The already verified Wayland/Thunar roundtrip must run on its minimal
# upstream Alpine image. Xorg's larger module/font stack is staged here.
set -eu
[ "$#" -eq 1 ] || { echo "usage: native-xorg.sh <riscv64|aarch64>" >&2; exit 2; }
arch=$1
case "$arch" in riscv64|aarch64) ;; *) exit 2 ;; esac
base="build/native-boot/$arch"
xroot="$base/xorg-root"
test -s "$base/desktop-root/sbin/init" || test -L "$base/desktop-root/sbin/init"
sudo rm -rf "$xroot"
mkdir -p "$xroot"
sudo cp -a "$base/desktop-root/." "$xroot/"
docker run --rm -v "$PWD/$xroot:/target" alpine:3.23 sh -ec "
    cp -a /target/etc/apk/keys/. /etc/apk/keys/
    apk --arch $arch --root /target --keys-dir /etc/apk/keys --initdb --no-scripts \
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
        add xorg-server xorg-server-dbg gdb xf86-input-evdev openbox xterm xeyes xcalc \
            xdpyinfo xsetroot xwininfo font-adobe-75dpi font-adobe-100dpi
"
sudo python3 - "$xroot" <<'PY'
from pathlib import Path
import sys
source = Path("tests/alpine-root.sh").read_text()
root = Path(sys.argv[1])
for tag, name in (("XORG", "xorg.conf"), ("INPUT", "xorg-input.py")):
    delim = "<<'" + tag + "'\n"
    start = source.index(delim) + len(delim)
    end = source.index("\n" + tag + "\n", start)
    path = root / "root" / name
    path.write_text(source[start:end] + "\n")
# No firmware framebuffer on native QEMU. Use the real DRM/GEM/KMS interface
# already qualified by original Alpine's libdrm and PageFlip tests.
config = root / "root/xorg.conf"
data = config.read_text()
assert 'Driver "fbdev"' in data
assert 'Option "fbdev" "/dev/fb0"' in data
data = data.replace('Driver "fbdev"', 'Driver "modesetting"')
data = data.replace('Option "fbdev" "/dev/fb0"',
                    'Option "kmsdev" "/dev/dri/card0"\n'
                    '    Option "AccelMethod" "none"')
config.write_text(data)
PY
disk="$base/xorg.ext4"
truncate -s 1536M "$disk"
sudo mkfs.ext4 -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' -d "$xroot" "$disk"
e2fsck -fn "$disk"
# The identical original Limine / NaSeed full kernel and native QEMU VM
# now boot a distinct genuine writable Xorg rootfs. Its own hardware
# QMP capture demonstrates actual X11 clients, not synthetic Xlib calls.
python3 tests/native-drm.py "$arch" xorg
e2fsck -fn "$disk"
echo "PASS: $arch Alpine Xorg / Openbox / xterm / xeyes on native DRM and real ext4"
