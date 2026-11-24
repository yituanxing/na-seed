#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
count=$(sh scripts/acceptance/check-elf-history.sh M144 scripts/m144-original-elfs.txt --count)
[ "$count" -eq 5 ]
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m144-batch
root=build/external/m144-batch/$1
test -s "$root/rootfs.cpio"
sh scripts/acceptance/hash-original-elfs.sh "$root/rootfs" scripts/m144-original-elfs.txt "$root/m144-original-elfs.sha256" 5 contained-file M144
echo '[M144 STAGED] five different original signed Alpine executables'
