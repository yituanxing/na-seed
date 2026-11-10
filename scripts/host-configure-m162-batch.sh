#!/bin/sh
# The HTTPS image and TLS private keys are *host-only*. Signed guest programs
# must receive the real PNG through VirtIO-Net before writing actual pixels.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port:$root" in
    amd64:build/external/m162-batch/amd64/rootfs|\
    riscv64:build/external/m162-batch/riscv64/rootfs|\
    arm64:build/external/m162-batch/arm64/rootfs) ;;
    *) exit 2 ;;
esac
work=${root%/rootfs}
sh scripts/host-configure-m156-https.sh "$port" "$root"
python3 - "$root/tests/m162-wire-image.sha256" "$work/host-m162-display.png" <<'PY'
from pathlib import Path
import hashlib
import struct
import sys
import zlib

def chunk(label, data):
    return struct.pack('>I', len(data)) + label + data + struct.pack(
        '>I', zlib.crc32(label + data) & 0xffffffff)

width, height = 512, 8
bands = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255))
rows = []
for y in range(height):
    row = bytearray()
    for x in range(width):
        rgb = bands[x // 128] if y == 0 else (
            (x * 7 + y * 17) & 255,
            (x * 13 + y * 29) & 255,
            ((x ^ y) * 11) & 255)
        row.extend(rgb)
    rows.append(b'\0' + bytes(row))
payload = b''.join(rows)
image = (b'\x89PNG\r\n\x1a\n' +
         chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
         chunk(b'tEXt', b'Title\0NaSeed M162 signed HTTPS to visible pixel hardware') +
         chunk(b'IDAT', zlib.compress(payload, 9)) +
         chunk(b'IEND', b''))
assert len(image) >= 200
Path(sys.argv[2]).write_bytes(image)
Path(sys.argv[1]).write_text(hashlib.sha256(image).hexdigest() + '\n', encoding='ascii')
PY
test -s "$root/tests/m156-https-ca.pem"
test -s "$root/tests/m162-wire-image.sha256"
test -s "$work/host-m162-display.png"
test -s "$work/host-tls.crt" && test -s "$work/host-tls.key"
test ! -e "$root/host-m162-display.png" && test ! -e "$root/host-tls.key"
echo "[M162 HOST IMAGE PASS] $port private key and real 512x8 RGB PNG stay outside guest"
