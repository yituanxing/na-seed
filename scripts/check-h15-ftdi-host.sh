#!/bin/sh
# H15 host-only FTDI xHCI model. Real acceptance also requires QEMU
# usb-serial and original signed Alpine userspace two-way socket proof.
set -eu
h15_bin="$(mktemp "${TMPDIR:-/tmp}/naseed-h15-ftdi-host.XXXXXX")"
trap 'rm -f "$h15_bin"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -fno-builtin \
    -I kernel \
    tests/h15-ftdi-host.c \
    kernel/drivers/usb/xhci.c kernel/drivers/port/dma.c \
    -o "$h15_bin"
"$h15_bin"
