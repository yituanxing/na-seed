#!/bin/sh
set -eu
[ "$#" -eq 2 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
test -d "$2/etc"
[ -s "$2/etc/passwd" ] || printf 'root:x:0:0:root:/tmp:/bin/sh\n' > "$2/etc/passwd"
[ -s "$2/etc/group" ] || printf 'root:x:0:\n' > "$2/etc/group"
