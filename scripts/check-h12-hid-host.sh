#!/bin/sh
# H12 independent host adversarial test of the actual USB HID boot decoder,
# Linux input core and evdev packet/ABI boundary. QEMU physical USB is separate.
set -eu
binary="${TMPDIR:-/tmp}/naseed-h12-hid-$$"
controller_binary="${TMPDIR:-/tmp}/naseed-h12-xhci-$$"
trap 'rm -f "$binary" "$controller_binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -fno-builtin \
    -I kernel \
    tests/h12-hid-boot-host.c \
    kernel/drivers/usb/hid_boot.c \
    kernel/drivers/input/core.c kernel/drivers/input/evdev.c \
    kernel/mm/heap.c kernel/lib/string.c \
    -o "$binary"
"$binary"
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -fno-builtin \
    -I kernel \
    tests/h12-xhci-hid-host.c \
    kernel/drivers/usb/xhci.c kernel/drivers/port/dma.c \
    -o "$controller_binary"
"$controller_binary"
