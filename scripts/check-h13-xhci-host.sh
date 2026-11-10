#!/bin/sh
# H13 genuine xHCI Boot Mouse transport host adversary: 4B Interrupt-IN,
# TRB completion lifetime, mode isolation, DMA halt and release invariants.
# Real QEMU USB hardware and original Alpine evtest remain separate CI gates.
set -eu
binary="${TMPDIR:-/tmp}/naseed-h13-xhci-$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -fno-builtin \
    -I kernel \
    tests/h13-xhci-mouse-host.c \
    kernel/drivers/usb/xhci.c kernel/drivers/port/dma.c \
    -o "$binary"
"$binary"
