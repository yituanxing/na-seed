#!/bin/sh
# Host contract for physical keyboard fan-out to the VT handler alongside
# the unchanged Linux evdev consumers. Real QMP input stays the native gate.
set -eu
mkdir -p build/m202-input-host
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m202-input-routing.c kernel/drivers/input/core.c \
    kernel/drivers/input/evdev.c kernel/mm/heap.c kernel/lib/string.c \
    -o build/m202-input-host/m202-input-routing
build/m202-input-host/m202-input-routing
