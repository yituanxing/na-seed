#!/bin/sh
set -eu
mkdir -p build/m186
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-omit-frame-pointer \
    -fsanitize=address,undefined -I kernel \
    scripts/test-m186-fbdev-node-pins.c \
    kernel/drivers/display/fbdev.c \
    kernel/drivers/display/fbdev_chrdev.c \
    kernel/drivers/core/chrdev.c -o build/m186/test-fbdev-node-pins
ASAN_OPTIONS=detect_leaks=0 build/m186/test-fbdev-node-pins pins
ASAN_OPTIONS=detect_leaks=0 build/m186/test-fbdev-node-pins collision
