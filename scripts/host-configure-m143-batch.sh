#!/bin/sh
# Profile-only minimal account metadata for genuine lighttpd root startup.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
test -d "$root/etc"
if [ ! -s "$root/etc/passwd" ]; then
    printf 'root:x:0:0:root:/tmp:/bin/sh\n' > "$root/etc/passwd"
fi
if [ ! -s "$root/etc/group" ]; then
    printf 'root:x:0:\n' > "$root/etc/group"
fi
