#!/bin/sh
# One Linux/POSIX native tty session test across all three architectures.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m17-tty.sh amd64|riscv64|arm64' >&2;exit 2; }
case "$1" in
    amd64) cc=gcc;emulator='' ;;
    riscv64) cc=riscv64-linux-gnu-gcc;emulator=qemu-riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc;emulator=qemu-aarch64 ;;
    *)exit 2;;
esac
dir="build/source-busybox/$1"
mkdir -p "$dir"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m17-tty.c -o "$dir/m17-tty"
if readelf -l "$dir/m17-tty" | grep -q INTERP; then
    echo '[M17] unexpected dynamic guest program' >&2;exit 1
fi
command -v script >/dev/null 2>&1 || { echo '[M17] missing Linux PTY script tool' >&2;exit 2; }
commandline="$dir/m17-tty"
if [ -n "$emulator" ]; then commandline="$emulator $commandline";fi
# A real PTY is necessary: never mock /dev/tty or set a pretend
# controlling foreground group when checking the Linux oracle.
output=$(script -q -e -c "$commandline" /dev/null | tr -d '\r')
printf '%s\n' "$output" | grep -Fxq m17-tty-ok || {
    echo "[M17 LINUX PTY FAIL] $1: $output" >&2;exit 1
}
if [ "$1" = amd64 ]; then
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        tests/m17-input.c -o "$dir/m17-input"
fi
echo "[M17 LINUX PTY PASS] $1 native tty/session/group oracle"
