#!/bin/sh
# Host-only roff/bibliography fixtures for 15 real GNU groff executables.
set -eu
[ "$#" -eq 2 ] || exit 2
case "$1" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
root=$2
[ "$root" = "build/external/m126-batch/$1/rootfs" ] || exit 2
test -d "$root"
mkdir -p "$root/tests"
python3 tests/m126-make-fixtures.py "$root/tests"
for name in basic.roff blank.roff equation.roff picture.roff table.roff \
            include.roff soelim.roff utf8.roff references.bib refer.roff \
            lookbib.query; do
    test -s "$root/tests/m126-$name"
done
echo "[M126 HOST CONFIG PASS] $1 signed GNU groff uses host-generated documents"
