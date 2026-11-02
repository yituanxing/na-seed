#!/bin/sh
# This profile-local host hook runs after signed Alpine APK installation,
# before the shared overlay builder's one authoritative CPIO serialization.
# Generate ephemeral TLS identities. Private keys never enter the guest root.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
mkdir -p "$root/tests"
work=$(dirname "$root")
command -v openssl >/dev/null
umask 077
openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 \
    -keyout "$work/host-ca.key" -out "$work/host-ca.crt" \
    -subj '/CN=NaSeed M156 ephemeral test CA' \
    -addext 'basicConstraints=critical,CA:TRUE' \
    -addext 'keyUsage=critical,keyCertSign,cRLSign' \
    > "$work/openssl-keygen.log" 2>&1
openssl req -new -newkey rsa:2048 -sha256 -nodes \
    -keyout "$work/host-tls.key" -out "$work/host-tls.csr" \
    -subj '/CN=m156.naseed.test' \
    -addext 'subjectAltName=DNS:m156.naseed.test,IP:10.0.2.2' \
    >> "$work/openssl-keygen.log" 2>&1
openssl x509 -req -in "$work/host-tls.csr" \
    -CA "$work/host-ca.crt" -CAkey "$work/host-ca.key" -CAcreateserial \
    -days 1 -sha256 -copy_extensions copy \
    -out "$work/host-tls.crt" >> "$work/openssl-keygen.log" 2>&1
openssl verify -CAfile "$work/host-ca.crt" -verify_hostname m156.naseed.test \
    "$work/host-tls.crt" >/dev/null
openssl verify -CAfile "$work/host-ca.crt" -verify_ip 10.0.2.2 \
    "$work/host-tls.crt" >/dev/null
cp "$work/host-ca.crt" "$root/tests/m156-https-ca.pem"
chmod 644 "$root/tests/m156-https-ca.pem"
echo '[M156 HTTPS CA STAGED] private keys host-only; guest owns one CA cert'
