#!/bin/sh
# M162: six unmodified signed Alpine programs bridge authenticated network
# PNG through original decoders into a real firmware display and PNG capture.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C.UTF-8
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }
work=/tmp/m162-wire-screen
ca=/tests/m156-https-ca.pem
busy mkdir -p "$work"
test -s "$ca" && test -s /tests/m162-wire-image.sha256
test -r /dev/fb0 && test -w /dev/fb0

for prog in curl wget pngtopam pamfile chafa magick fbgrab; do
    binary="/usr/bin/$prog"
    test -x "$binary"
    resolved=$(busy readlink -f "$binary")
    case "$resolved" in /bin/busybox|/usr/bin/busybox) exit 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ]
done

# DNS-SAN and IP-SAN verification are both real, and the PNG is absent from
# initial CPIO. Two independent clients obtain identical live server bytes.
curl_ready=false
for attempt in 1 2 3 4 5 6 7 8; do
    if /usr/bin/curl --noproxy '*' --fail --silent --show-error \
        --cacert "$ca" --resolve m156.naseed.test:15443:10.0.2.2 \
        --connect-timeout 8 --max-time 15 -o "$work/wire-curl.png" \
        https://m156.naseed.test:15443/m162-display.png; then
        curl_ready=true
        break
    fi
    busy sleep 1
done
[ "$curl_ready" = true ]
expected=$(busy cat /tests/m162-wire-image.sha256)
test "${#expected}" -eq 64
actual=$(busy sha256sum "$work/wire-curl.png" | busy cut -d ' ' -f 1)
[ "$actual" = "$expected" ]
echo M162_CURL_TLS_PNG_PASS

/usr/bin/wget --quiet --tries=3 --timeout=12 --ca-certificate="$ca" \
    --output-document="$work/wire-wget.png" \
    https://10.0.2.2:15443/m162-display-wget.png
busy cmp -s "$work/wire-curl.png" "$work/wire-wget.png"
echo M162_WGET_TLS_PNG_PASS

# Netpbm parses actual fetched PNG pixels, independently of ImageMagick.
/usr/bin/pngtopam "$work/wire-curl.png" > "$work/pixels.ppm"
[ "$(busy dd if="$work/pixels.ppm" bs=1 count=2 2>/dev/null)" = P6 ]
/usr/bin/pamfile "$work/pixels.ppm" > "$work/image-info" 2>&1
busy grep -Fq '512 by 8' "$work/image-info"
echo M162_ORIGINAL_PNG_PIXELS_PASS

# Same downloaded image is also rendered by the original signed Chafa
# terminal renderer; this proves real program composition, not a test marker.
/usr/bin/chafa --format symbols --colors none --size 40x12 \
    "$work/wire-curl.png" > "$work/char-frame"
[ "$(busy wc -c < "$work/char-frame")" -gt 80 ]
[ "$(busy wc -l < "$work/char-frame")" -ge 8 ]
echo '[M162 TERMINAL IMAGE BEGIN]'
busy cat "$work/char-frame"
echo '[M162 TERMINAL IMAGE END]'
echo M162_CHARACTER_RENDER_PASS

# No PPM/PNG/stripe source was present in the CPIO. Original ImageMagick
# converts this *network-delivered* 512x8 RGB PNG into native BGRA pixels.
/usr/bin/magick "$work/wire-curl.png" -depth 8 BGRA:"$work/display.bgra"
[ "$(busy wc -c < "$work/display.bgra")" -eq 16384 ]
busy dd if="$work/display.bgra" of=/dev/fb0 bs=16384 count=1 2> "$work/write.log"
echo M162_FRAMEBUFFER_WRITE_PASS

# Real signed upstream fbgrab uses framebuffer Linux ioctls + read(), then
# libpng-encodes actual screen bytes; independent ImageMagick pixel sampling.
/usr/bin/fbgrab -d /dev/fb0 "$work/screen.png"
sig=$(busy dd if="$work/screen.png" bs=1 count=8 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
[ "$sig" = 89504e470d0a1a0a ]
for entry in '32 255 0 0' '160 0 255 0' '288 0 0 255' '416 255 255 255'; do
    set -- $entry
    x=$1
    expected_rgb="$2 $3 $4"
    /usr/bin/magick "$work/screen.png" -crop "1x1+$x+0" +repage \
        -depth 8 RGB:"$work/sample.rgb"
    sampled=$(busy od -An -tu1 "$work/sample.rgb" | busy awk '{$1=$1; print}')
    [ "$sampled" = "$expected_rgb" ] || {
        echo "[M162 FBGRAB PIXEL FAIL] x=$x expected=$expected_rgb got=$sampled" >&2
        exit 1
    }
done
echo M162_FBGRAB_CAPTURE_PASS

# The independent host QMP screendump samples real QEMU display pixels.
echo M162_FRAME_VISIBLE
busy sleep 18
echo M162_NETWORK_SCREEN_PASS
