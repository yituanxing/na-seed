#!/bin/sh
set -eu
binary="${TMPDIR:-/tmp}/naseed-h4-nvme-readonly-$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror \
    -I kernel -I kernel/drivers -I kernel/drivers/nvme \
    tests/h4-nvme-readonly-host.c \
    kernel/drivers/nvme/readonly.c \
    kernel/drivers/port/dma.c \
    -o "$binary"
"$binary"
