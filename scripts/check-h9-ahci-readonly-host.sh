#!/bin/sh
# H9 protocol-level fake AHCI host regression.  Not a real QEMU acceptance.
set -eu
binary="${TMPDIR:-/tmp}/naseed-h9-ahci-readonly-$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror \
    -I kernel -I kernel/drivers -I kernel/drivers/ahci \
    tests/h9-ahci-readonly-host.c \
    kernel/drivers/ahci/readonly.c \
    kernel/drivers/port/dma.c \
    -o "$binary"
"$binary"
