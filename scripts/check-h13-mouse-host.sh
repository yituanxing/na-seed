#!/bin/sh
# Independent H13 Boot Mouse report -> input core -> evdev ABI host gate.
# The actual QEMU USB and signed Alpine evtest proof lives in H13 CI.
set -eu
binary="${TMPDIR:-/tmp}/naseed-h13-mouse-$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -fno-builtin \
    -I kernel \
    tests/h13-hid-mouse-host.c \
    kernel/drivers/usb/hid_boot_mouse.c \
    kernel/drivers/input/core.c kernel/drivers/input/evdev.c \
    kernel/mm/heap.c kernel/lib/string.c \
    -o "$binary"
"$binary"
