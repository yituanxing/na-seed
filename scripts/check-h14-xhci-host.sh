#!/bin/sh
# H14 adversarial host-only simulation: one xHC, two independently routed slots.
# This test does not prove real USB input or original Alpine evtest.
set -eu
h14_bin="${TMPDIR:-/tmp}/naseed-h14-dual-xhci-$$"
trap 'rm -f "$h14_bin"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -fno-builtin \
    -I kernel \
    tests/h14-xhci-dual-host.c \
    kernel/drivers/usb/xhci.c kernel/drivers/port/dma.c \
    -o "$h14_bin"
"$h14_bin"
