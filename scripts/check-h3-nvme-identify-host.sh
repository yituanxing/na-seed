#!/bin/sh
set -eu
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror \
    -I kernel -I kernel/drivers -I kernel/drivers/nvme \
    tests/h3-nvme-identify-host.c \
    kernel/drivers/nvme/identify.c \
    kernel/drivers/port/dma.c \
    -o /tmp/naseed-h3-nvme-identify-host-$$
"/tmp/naseed-h3-nvme-identify-host-$$"
rm -f "/tmp/naseed-h3-nvme-identify-host-$$"
