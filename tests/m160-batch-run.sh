#!/bin/sh
# M160: no PNG image exists in the guest's initial CPIO. Two distinct original
# Alpine HTTPS clients fetch actual bytes from an independent TLS host, then
# unchanged signed image programs decode real pixels and render Unicode art.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C.UTF-8
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }
work=/tmp/m160-image
ca=/tests/m156-https-ca.pem
test -s "$ca" && test -s /tests/m160-image.sha256
busy mkdir -p "$work"

original() {
    binary="/usr/bin/$1"
    test -x "$binary" || return 1
    resolved=$(busy readlink -f "$binary") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$binary"
}
for executable in curl wget pngtopam pamfile pamtable chafa; do
    original "$executable" >/dev/null || exit 1
done

expected=$(busy cat /tests/m160-image.sha256)
case "$expected" in
    *[!0-9a-f]*|'') echo '[M160 FAIL] malformed host truth hash' >&2; exit 1 ;;
esac
[ "${#expected}" -eq 64 ] || exit 1
image_check() {
    downloaded=$1
    [ -s "$downloaded" ] || return 1
    bytes=$(busy wc -c < "$downloaded")
    [ "$bytes" -ge 200 ] && [ "$bytes" -le 65536 ] || return 1
    sig=$(busy dd if="$downloaded" bs=1 count=8 2>/dev/null |
          busy od -An -tx1 | busy tr -d ' \n')
    [ "$sig" = 89504e470d0a1a0a ] || return 1
    actual=$(busy sha256sum "$downloaded" | busy cut -d ' ' -f 1)
    [ "$actual" = "$expected" ]
}

# This route uses the DNS SAN of the ephemeral signed leaf. --resolve only
# bypasses DNS discovery; libcurl still checks the complete X.509 identity.
curl_ready=false
for attempt in 1 2 3 4 5 6 7 8; do
    if /usr/bin/curl --noproxy '*' --fail --silent --show-error \
        --cacert "$ca" --resolve m156.naseed.test:15443:10.0.2.2 \
        --connect-timeout 8 --max-time 15 \
        -o "$work/curl.png" \
        https://m156.naseed.test:15443/m160-curl.png; then
        curl_ready=true
        break
    fi
    busy printf '[M160 HTTPS DHCP WAIT] curl attempt=%s\n' "$attempt"
    busy sleep 1
done
[ "$curl_ready" = true ] && image_check "$work/curl.png"
echo M160_REAL_CURL_IMAGE_TLS_PASS

# GNU wget independently verifies the matching IP SAN (not --no-check-*).
/usr/bin/wget --quiet --tries=3 --timeout=12 --ca-certificate="$ca" \
    --output-document="$work/wget.png" \
    https://10.0.2.2:15443/m160-wget.png
image_check "$work/wget.png"
busy cmp -s "$work/curl.png" "$work/wget.png"
echo M160_REAL_WGET_IMAGE_TLS_PASS

# Original Netpbm libpng consumer parses the actual downloaded PNG and
# materializes pixel data; do not accept a MIME type or PNG header alone.
# Netpbm pngtopam outputs raw PPM (P6) for an RGB PNG without alpha;
# P7 is only forced by -alphapam. Require the true upstream wire format.
/usr/bin/pngtopam "$work/curl.png" > "$work/decoded.ppm" 2> "$work/png-error"
[ -s "$work/decoded.ppm" ]
magic=$(busy dd if="$work/decoded.ppm" bs=1 count=2 2>/dev/null)
[ "$magic" = P6 ]
/usr/bin/pamfile "$work/decoded.ppm" > "$work/pam-info" 2>&1
busy grep -Fq '32 by 24' "$work/pam-info"
/usr/bin/pamtable "$work/decoded.ppm" > "$work/pixels" 2> "$work/pam-error"
[ "$(busy wc -l < "$work/pixels")" -eq 24 ]
[ "$(busy sort -u "$work/pixels" | busy wc -l)" -ge 12 ]
busy grep -qE '[1-9]' "$work/pixels"
echo M160_REAL_PNG_DECODE_PASS

# Original signed Chafa converts exactly those network-delivered pixels to
# actual character glyphs. Print the frame to guest serial, not a fake PASS.
/usr/bin/chafa --format symbols --colors none --size 32x16 \
    "$work/curl.png" > "$work/frame.txt" 2> "$work/chafa-error"
[ -s "$work/frame.txt" ]
[ "$(busy wc -l < "$work/frame.txt")" -ge 12 ]
[ "$(busy wc -c < "$work/frame.txt")" -ge 100 ]
[ "$(busy sort -u "$work/frame.txt" | busy wc -l)" -ge 4 ]
echo '[M160 REAL CHAR FRAME BEGIN]'
busy cat "$work/frame.txt"
echo '[M160 REAL CHAR FRAME END]'
echo M160_REAL_CHAR_RENDER_PASS
echo M160_WIRE_IMAGE_PASS
