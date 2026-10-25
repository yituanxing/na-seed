#!/bin/sh
# Single three-ISA guest scenario; all actual application work is in NaSeed.
set -eu
stage=bash
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo "[M58 FAIL] stage=$stage rc=$rc" >&2; fi' EXIT
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
TERM=vt100
export PATH HOME LC_ALL TERM

# Verify we really launch the signed Alpine Bash rather than BusyBox ash.
test -x /bin/bash
/bin/bash --noprofile --norc -c 'test -n "$BASH_VERSION" && test -n "$BASH"'
echo M58_ORIGINAL_SIGNED_BASH_PASS
/bin/bash --noprofile --norc /tests/m58-bash-suite.sh
test "$(/bin/busybox cat /tmp/m58-bash/trap.txt)" = cleanup
echo M58_BASH_TRAP_PASS

stage=tcl
test -x /usr/bin/tclsh
/usr/bin/tclsh /tests/m58-tcl-suite.tcl
echo M58_ORIGINAL_SIGNED_TCL_PASS

stage=openssl
test -x /usr/bin/openssl
case "$(/usr/bin/openssl version)" in OpenSSL\ 3.*) ;; *) exit 1 ;; esac
echo M58_ORIGINAL_SIGNED_OPENSSL_PASS

# Real entropy and crypto. No TLS networking, no native compilation.
rand=$(/usr/bin/openssl rand -hex 16)
test "${#rand}" -eq 32
echo M58_OPENSSL_ENTROPY_PASS
printf abc > /tmp/m58-crypto-in
/usr/bin/openssl dgst -sha256 /tmp/m58-crypto-in > /tmp/m58-crypto-digest
/bin/busybox grep -Fqi 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad' /tmp/m58-crypto-digest
echo M58_OPENSSL_DIGEST_PASS
/usr/bin/openssl enc -aes-256-cbc -pbkdf2 -salt -pass pass:naseed-m58 \
    -in /tmp/m58-crypto-in -out /tmp/m58-crypto.enc
/usr/bin/openssl enc -d -aes-256-cbc -pbkdf2 -pass pass:naseed-m58 \
    -in /tmp/m58-crypto.enc -out /tmp/m58-crypto-out
/bin/busybox cmp -s /tmp/m58-crypto-in /tmp/m58-crypto-out
echo M58_OPENSSL_ROUNDTRIP_PASS

stage=certificate
/usr/bin/openssl genpkey -algorithm ED25519 -out /tmp/m58-key.pem
/usr/bin/openssl req -new -x509 -key /tmp/m58-key.pem \
    -out /tmp/m58-cert.pem -days 1 -subj '/CN=NaSeed-M58'
/usr/bin/openssl verify -CAfile /tmp/m58-cert.pem /tmp/m58-cert.pem \
    > /tmp/m58-cert-verify.txt
/bin/busybox grep -Fq 'OK' /tmp/m58-cert-verify.txt
echo M58_OPENSSL_CERT_VERIFY_PASS
echo M58_RUNTIME_FULL_PASS
