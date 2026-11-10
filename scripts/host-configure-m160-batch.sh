#!/bin/sh
# Host-only real PNG fixture and ephemeral, fully-verified TLS identity.
# Reuse the tested M156 CA implementation. Host private keys and PNG bytes
# never enter the guest image: the latter must arrive via the real NIC.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$root" in build/external/m160-batch/*/rootfs) ;; *) exit 2 ;; esac
work=${root%/rootfs}
test "$work" != "$root" || exit 2

# Proven host CA/leaf, certificate DNS+IP SAN and key isolation are owned by
# M156; don't create an independent test PKI for every application profile.
sh scripts/host-configure-m156-https.sh "$port" "$root"

python3 - "$root/tests" "$work" <<'PY'
from pathlib import Path
import hashlib
import struct
import sys
import zlib

guest_tests, host_work = map(Path, sys.argv[1:])
guest_tests.mkdir(parents=True, exist_ok=True)

def chunk(tag, payload):
    return (struct.pack('>I', len(payload)) + tag + payload
            + struct.pack('>I', zlib.crc32(tag + payload) & 0xffffffff))

width, height = 32, 24
rgb = lambda x, y: ((x * 17 + y * 3) & 255,
                    (y * 31 + x * 5) & 255,
                    ((x ^ y) * 11) & 255)
scanlines = b''.join(
    b'\0' + b''.join(bytes(rgb(x, y)) for x in range(width))
    for y in range(height)
)
image = (b'\x89PNG\r\n\x1a\n'
         + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
         + chunk(b'tEXt', b'Title\0NaSeed M160 real HTTPS image')
         + chunk(b'IDAT', zlib.compress(scanlines, 9))
         + chunk(b'IEND', b''))
assert len(image) > 200 and image[:8] == b'\x89PNG\r\n\x1a\n'
(host_work / 'host-m160-image.png').write_bytes(image)
(guest_tests / 'm160-image.sha256').write_text(
    hashlib.sha256(image).hexdigest() + '\n', encoding='ascii')
PY
test -s "$work/host-m160-image.png"
test -s "$root/tests/m160-image.sha256"
test -s "$root/tests/m156-https-ca.pem"
test -s "$work/host-tls.crt" && test -s "$work/host-tls.key"
test ! -e "$root/host-tls.key" && test ! -e "$root/host-ca.key"
echo "[M160 IMAGE HOST FIXTURE PASS] $port valid nonconstant RGB PNG remains host-only until network GET"
