#!/bin/sh
# H10 fake E1000 register/DMA protocol regression; REAL QEMU acceptance is
# separate and remains mandatory for claiming actual PCI/network support.
set -eu
binary="${TMPDIR:-/tmp}/naseed-h10-e1000-host-$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror \
    -I kernel -I kernel/drivers -I kernel/drivers/e1000 \
    tests/h10-e1000-host.c kernel/drivers/e1000/e1000_nic.c \
    kernel/drivers/port/dma.c -o "$binary"
"$binary"
