#!/bin/sh
# Host-only real-data fixtures for three unchanged Alpine executable families.
# Official APK programs are NEVER compiled, relinked or replaced in the guest.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
case "$root" in build/external/m159-batch/*/rootfs) ;; *) exit 2 ;; esac
mkdir -p "$root/tests"
python3 - "$root/tests" <<'PY'
from pathlib import Path
import sys

d = Path(sys.argv[1])
rgb = [(x * 32, y * 32, (x + y) * 16) for y in range(8) for x in range(8)]
ppm = b"P3\n8 8\n255\n" + b"".join(
    ("%d %d %d\n" % color).encode("ascii") for color in rgb
)
pgm = b"P2\n8 8\n255\n" + b"".join(
    ("%d\n" % ((x * 29 + y * 13) % 256)).encode("ascii")
    for y in range(8) for x in range(8)
)
pbm = b"P1\n8 8\n" + b"".join(
    ("%d\n" % ((x + y) % 2)).encode("ascii")
    for y in range(8) for x in range(8)
)
(d / "m159-color.ppm").write_bytes(ppm)
(d / "m159-gray.pgm").write_bytes(pgm)
(d / "m159-bw.pbm").write_bytes(pbm)
(d / "m159-multi.pnm").write_bytes(ppm + pgm)
(d / "m159-rgb.txt").write_text(
    "0 0 0 black\n255 255 255 white\n128 128 128 gray\n",
    encoding="ascii",
)
(d / "m159-svn-note.txt").write_text(
    "NaSeed M159 original Subversion repository commit\n",
    encoding="ascii",
)
PY

# Test-only PKCS#12 is created by genuine host OpenSSL, consumed by the
# original NSS pk12util. Host PEM private key is not placed in the guest.
command -v openssl >/dev/null
work=$(dirname "$root")
umask 077
openssl req -x509 -nodes -newkey rsa:2048 -sha256 -days 1 \
    -keyout "$work/m159-host-key.pem" \
    -out "$work/m159-host-cert.pem" \
    -subj '/CN=NaSeed M159 ephemeral NSS fixture' \
    > "$work/m159-openssl.log" 2>&1
openssl pkcs12 -export -in "$work/m159-host-cert.pem" \
    -inkey "$work/m159-host-key.pem" \
    -out "$root/tests/m159-test-cert.p12" \
    -name m159-nss-fixture -passout pass:m159-fixture \
    >> "$work/m159-openssl.log" 2>&1
openssl pkcs12 -in "$root/tests/m159-test-cert.p12" \
    -passin pass:m159-fixture -nokeys -noout >> "$work/m159-openssl.log" 2>&1
chmod 644 "$root/tests/m159-test-cert.p12"
for input in m159-color.ppm m159-gray.pgm m159-bw.pbm \
             m159-multi.pnm m159-rgb.txt m159-svn-note.txt m159-test-cert.p12; do
    test -s "$root/tests/$input" || exit 1
done
echo "[M159 HOST FIXTURES PASS] $port real PNM, SVN revision and ephemeral NSS PKCS12"
