#!/bin/sh
# Compile gettext binary message catalog on the Linux HOST, never inside NaSeed.
# APK signatures are independently checked during signed overlay installation.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
command -v msgfmt >/dev/null || {
    echo "[M119 HOST FIXTURE FAIL] msgfmt not installed on the Linux host" >&2
    exit 1
}
mkdir -p "$root/tests"
msgfmt -o "$root/tests/m119-catalog.mo" tests/m119-catalog.po
test -s "$root/tests/m119-catalog.mo"
echo "[M119 HOST FIXTURE PASS] $port genuine gettext .po to .mo compiled by host"
