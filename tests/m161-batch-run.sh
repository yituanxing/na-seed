#!/bin/sh
# First actual OS graphics vertical slice: signed ImageMagick -> /dev/fb0
# -> original Alpine fbgrab PNG. QEMU QMP independently checks physical output.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
export PATH HOME
busy() { /bin/busybox "$@"; }

for prog in fbgrab magick; do
    binary="/usr/bin/$prog"
    test -x "$binary"
    resolved=$(busy readlink -f "$binary")
    case "$resolved" in /bin/busybox|/usr/bin/busybox) exit 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || exit 1
done

test -r /dev/fb0 && test -w /dev/fb0
test -s /tests/m161-stripes.ppm
# This verifies conditional device exposure; the host will separately require
# the *actual* Limine mode to report 32-bit R16/G8/B0 before accepting BGRA.
echo M161_FB_DEVICE_OPEN_PASS

/usr/bin/magick /tests/m161-stripes.ppm -depth 8 BGRA:/tmp/m161-pixels.bgra
[ "$(busy wc -c < /tmp/m161-pixels.bgra)" -eq 2048 ]
busy dd if=/tmp/m161-pixels.bgra of=/dev/fb0 bs=2048 count=1 2>/tmp/m161-write.log
echo M161_MAGICK_PIXEL_WRITE_PASS

# fbgrab uses open/read + exact Linux FBIOGET_* ioctls and encodes actual
# framebuffer contents with the signed, unmodified Alpine libpng.
/usr/bin/fbgrab -d /dev/fb0 /tmp/m161-screen.png
[ -s /tmp/m161-screen.png ]
sig=$(busy dd if=/tmp/m161-screen.png bs=1 count=8 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
[ "$sig" = 89504e470d0a1a0a ]

sample() {
    x=$1 expected=$2
    /usr/bin/magick /tmp/m161-screen.png -crop "1x1+$x+0" +repage -depth 8 RGB:/tmp/m161-sample.rgb
    actual=$(busy od -An -tu1 /tmp/m161-sample.rgb | busy awk '{$1=$1; print}')
    [ "$actual" = "$expected" ] || {
        echo "[M161 PIXEL FAIL] x=$x expected=$expected actual=$actual" >&2
        return 1
    }
}
sample 32 '255 0 0'
sample 160 '0 255 0'
sample 288 '0 0 255'
sample 416 '255 255 255'
echo M161_FBGRAB_READ_PNG_PASS

# Host QMP watcher starts a screendump only upon this marker, so it captures
# the emulated display, not an arbitrary guest-managed byte array.
echo M161_SCREEN_DRAWN
busy sleep 18
echo M161_FRAMEBUFFER_PASS
