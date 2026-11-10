#!/bin/sh
# H11 real MSC wire protocol host fake; genuine QEMU USB xHCI validation separate.
set -eu
binary="${TMPDIR:-/tmp}/naseed-h11-usb-msc-$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror \
    -I kernel -I kernel/drivers -I kernel/drivers/usb \
    tests/h11-usb-msc-host.c \
    kernel/drivers/usb/msc.c \
    -o "$binary"
"$binary"
