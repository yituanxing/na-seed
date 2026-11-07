#!/bin/sh
# M156 is a TLS semantic regression of the existing signed M143 Alpine ELFs.
# The shared profile catalog is the only package/ELF/copy identity authority.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: sh scripts/stage-m156-prebuilt.sh PORT' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m156-https
root="build/external/m156-https/$port"
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/tests/m156-https-ca.pem"
test -s "$root/host-ca.crt"
echo "[M156 STAGED] $port existing signed curl/wget, independent TLS CA oracle; guest proof pending"
