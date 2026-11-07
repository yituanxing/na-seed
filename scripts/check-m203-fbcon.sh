#!/bin/sh
set -eu
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naseed-m203-fbcon.XXXXXXXX")
trap 'rm -f "$temp_dir/fbcon-test"; rmdir "$temp_dir"' EXIT HUP INT TERM
gcc -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin \
    -I . -I kernel -fsanitize=address,undefined -fno-omit-frame-pointer \
    scripts/test-m203-fbcon.c kernel/drivers/display/fbcon.c \
    -o "$temp_dir/fbcon-test"
ASAN_OPTIONS=detect_leaks=0 "$temp_dir/fbcon-test"
